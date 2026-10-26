/*
 * the dispatch core of the scripting objects: the members of an object come from its table, as msi.dll's own
 * objects do, with no type library behind them. how a call is looked up, how its arguments are read and how a
 * failure is reported follows the installer of windows; see docs/msi.md.
 */
#include "automation.h"
#include <mime/win32/winver.h>

MIME_DEFINE_GUID(IID_AutoInstaller, 0x000C1090, 0, 0, 0xC0, 0, 0, 0, 0, 0, 0, 0x46);
MIME_DEFINE_GUID(IID_AutoTypeLib, 0x000C1092, 0, 0, 0xC0, 0, 0, 0, 0, 0, 0, 0x46);
MIME_DEFINE_GUID(IID_AutoRecord, 0x000C1093, 0, 0, 0xC0, 0, 0, 0, 0, 0, 0, 0x46);
MIME_DEFINE_GUID(IID_AutoStringList, 0x000C1095, 0, 0, 0xC0, 0, 0, 0, 0, 0, 0, 0x46);
MIME_DEFINE_GUID(IID_AutoRecordList, 0x000C1096, 0, 0, 0xC0, 0, 0, 0, 0, 0, 0, 0x46);
MIME_DEFINE_GUID(IID_AutoComponent, 0x000C1097, 0, 0, 0xC0, 0, 0, 0, 0, 0, 0, 0x46);
MIME_DEFINE_GUID(IID_AutoComponentClient, 0x000C1098, 0, 0, 0xC0, 0, 0, 0, 0, 0, 0, 0x46);
MIME_DEFINE_GUID(IID_AutoComponentPath, 0x000C1099, 0, 0, 0xC0, 0, 0, 0, 0, 0, 0, 0x46);
MIME_DEFINE_GUID(IID_AutoUIPreview, 0x000C109A, 0, 0, 0xC0, 0, 0, 0, 0, 0, 0, 0x46);
MIME_DEFINE_GUID(IID_AutoSummaryInfo, 0x000C109B, 0, 0, 0xC0, 0, 0, 0, 0, 0, 0, 0x46);
MIME_DEFINE_GUID(IID_AutoView, 0x000C109C, 0, 0, 0xC0, 0, 0, 0, 0, 0, 0, 0x46);
MIME_DEFINE_GUID(IID_AutoDatabase, 0x000C109D, 0, 0, 0xC0, 0, 0, 0, 0, 0, 0, 0x46);
MIME_DEFINE_GUID(IID_AutoSession, 0x000C109E, 0, 0, 0xC0, 0, 0, 0, 0, 0, 0, 0x46);
MIME_DEFINE_GUID(IID_AutoFeatureInfo, 0x000C109F, 0, 0, 0xC0, 0, 0, 0, 0, 0, 0, 0x46);
MIME_DEFINE_GUID(IID_AutoProduct, 0x000C10A0, 0, 0, 0xC0, 0, 0, 0, 0, 0, 0, 0x46);
MIME_DEFINE_GUID(IID_AutoPatch, 0x000C10A1, 0, 0, 0xC0, 0, 0, 0, 0, 0, 0, 0x46);
MIME_DEFINE_GUID(IID_AutoObjectSafety, 0xCB5BDC81, 0x93C1, 0x11CF, 0x8F, 0x20, 0x00, 0x80, 0x5F, 0x2C, 0xD0, 0x64);

LONG MimeMsiAutoInstances;
static const VARIANT empty_variant;

/* ------------------------------------------------------------------ lifetime */

static HRESULT WINAPI auto_query_interface(IDispatch *self, REFIID iid, void **out)
{
    AUTO_OBJECT *object = (AUTO_OBJECT *)self;
    if (IsEqualGUID(iid, object->Iid) || IsEqualGUID(iid, &IID_IDispatch) || IsEqualGUID(iid, &IID_IUnknown)) {
        *out = self;
        AutoAddRef(object);
        return S_OK;
    }
    if (object->QueryExtra)
        return object->QueryExtra(object, iid, out);
    *out = nullptr;
    return E_NOINTERFACE;
}

static ULONG WINAPI auto_add_ref(IDispatch *self)
{
    return (ULONG)InterlockedIncrement(&((AUTO_OBJECT *)self)->References);
}

static ULONG WINAPI auto_release(IDispatch *self)
{
    AUTO_OBJECT *object = (AUTO_OBJECT *)self;
    LONG references = InterlockedDecrement(&object->References);
    if (!references) {
        MSIHANDLE handle = object->Handle;
        object->Destroy(object);
        InterlockedDecrement(&MimeMsiAutoInstances);
        if (handle)
            MsiCloseHandle(handle);
    }
    return (ULONG)references;
}

void AutoAddRef(AUTO_OBJECT *object)
{
    InterlockedIncrement(&object->References);
}

void AutoRelease(AUTO_OBJECT *object)
{
    object->lpVtbl->Release((IDispatch *)object);
}

/* ------------------------------------------------------------------ names and calls */

static HRESULT WINAPI auto_type_info_count(IDispatch *self, UINT *count)
{
    if (!count)
        return E_POINTER;
    *count = 0;
    return S_OK;
}

static HRESULT WINAPI auto_type_info(IDispatch *self, UINT index, LCID lcid, ITypeInfo **info)
{
    if (!info)
        return E_POINTER;
    *info = nullptr;
    return DISP_E_BADINDEX;
}

/* names match with the case bit of a letter ignored, as the installer does, and end at a separator. */
static BOOL name_char_equal(WCHAR a, WCHAR b)
{
    return ((a ^ b) & ~0x20) == 0;
}

static HRESULT WINAPI auto_get_ids_of_names(IDispatch *self, REFIID iid, LPOLESTR *names, UINT count, LCID lcid,
                                            DISPID *ids)
{
    AUTO_OBJECT *object = (AUTO_OBJECT *)self;
    if (!count || !names || !ids)
        return E_INVALIDARG;
    ids[0] = -1;
    for (ULONG i = 0; i < object->Count; i++) {
        const AUTO_ENTRY *entry = &object->Entries[i];
        PCWSTR table = entry->Names;
        PCWSTR input = names[0];
        while (*input && name_char_equal(*input, *table)) {
            input++;
            table++;
        }
        if (*input || *table >= 0x30)
            continue;
        ids[0] = entry->Id;
        UINT remaining = count - 1;
        DISPID *next = ids + 1;
        /*
         * each further name is a parameter, numbered from zero in the order of the table. a name that is the start
         * of a parameter matches it, and the id of a name that is not found is overwritten by the next one.
         */
        for (UINT n = 1; n < count; n++) {
            *next = -1;
            PCWSTR at = table;
            DISPID index = 0;
            while (*at) {
                PCWSTR name = names[n];
                PCWSTR candidate = at + 1;
                while (*name && name_char_equal(*name, *candidate)) {
                    name++;
                    candidate++;
                }
                if (!*name) {
                    *next++ = index;
                    remaining--;
                    break;
                }
                at = candidate;
                while (*at >= 0x30)
                    at++;
                index++;
            }
        }
        return remaining ? DISP_E_UNKNOWNNAME : S_OK;
    }
    return DISP_E_UNKNOWNNAME;
}

HRESULT AutoWin32ToHresult(UINT error)
{
    return error > 0 && error < 0x8000 ? (HRESULT)(0x80070000 | error) : (HRESULT)error;
}

static HRESULT WINAPI auto_invoke(IDispatch *self, DISPID id, REFIID iid, LCID lcid, WORD flags, DISPPARAMS *params,
                                  VARIANT *result, EXCEPINFO *excep, UINT *arg_error)
{
    AUTO_OBJECT *object = (AUTO_OBJECT *)self;
    const AUTO_ENTRY *entry = nullptr;
    for (ULONG i = 0; i < object->Count; i++)
        if (object->Entries[i].Id == id) {
            entry = &object->Entries[i];
            break;
        }
    if (!entry || !(flags & entry->Flags))
        return DISP_E_MEMBERNOTFOUND;
    if (!params || params->cNamedArgs > params->cArgs || (params->cArgs && !params->rgvarg))
        return E_INVALIDARG;
    AUTO_ARGS args = {params->cArgs, params->cNamedArgs, params->rgdispidNamedArgs, params->rgvarg,
                      (flags & (DISPATCH_PROPERTYPUT | DISPATCH_PROPERTYPUTREF)) ? nullptr : result, flags};
    if (args.Result && args.Result->vt)
        VariantClear(args.Result);
    HRESULT hr = entry->Method(object, &args);
    /* a result the installer reports as an error that is not one of the dispatch errors is raised as an exception. */
    if (hr && (hr & AUTO_WIN32_ERROR_MASK) != 0x80020000) {
        if (hr >= 0)
            hr = E_FAIL;
        if (excep) {
            excep->wCode = 1000;
            excep->wReserved = 0;
            excep->bstrSource = SysAllocString(L"Msi API Error");
            excep->bstrDescription = SysAllocString(entry->Names);
            excep->bstrHelpFile = SysAllocString(L"Msi.chm");
            excep->dwHelpContext = entry->HelpContext;
            excep->scode = hr;
            excep->pfnDeferredFillIn = nullptr;
            hr = DISP_E_EXCEPTION;
        }
    }
    return hr;
}

static const IDispatchVtbl auto_vtbl = {
    auto_query_interface, auto_add_ref, auto_release, auto_type_info_count, auto_type_info, auto_get_ids_of_names,
    auto_invoke,
};

void AutoInit(AUTO_OBJECT *object, const AUTO_ENTRY *entries, ULONG count, const GUID *iid, MSIHANDLE handle,
              void (*destroy)(AUTO_OBJECT *))
{
    object->lpVtbl = &auto_vtbl;
    object->References = 1;
    object->Entries = entries;
    object->Count = count;
    object->Iid = iid;
    object->Handle = handle;
    object->Destroy = destroy;
    InterlockedIncrement(&MimeMsiAutoInstances);
}

static void destroy_plain(AUTO_OBJECT *object)
{
    if (object->Owner)
        AutoRelease(object->Owner);
    MimeMsiFree(object);
}

AUTO_OBJECT *AutoAllocate(size_t size, const AUTO_ENTRY *entries, ULONG count, const GUID *iid, MSIHANDLE handle,
                          void (*destroy)(AUTO_OBJECT *))
{
    AUTO_OBJECT *object = MimeMsiAllocate(size);
    if (!object) {
        if (handle)
            MsiCloseHandle(handle);
        return nullptr;
    }
    AutoInit(object, entries, count, iid, handle, destroy ? destroy : destroy_plain);
    return object;
}

/* ------------------------------------------------------------------ arguments */

static int argument_index(const AUTO_ARGS *args, UINT index)
{
    if (index && index <= args->Count - args->NamedCount)
        return (int)(args->Count - index);
    DISPID wanted = index ? (DISPID)index - 1 : DISPID_PROPERTYPUT;
    for (int i = (int)args->NamedCount - 1; i >= 0; i--)
        if (args->NamedIds && args->NamedIds[i] == wanted)
            return i;
    return -1;
}

VARIANT *AutoArgument(AUTO_ARGS *args, UINT index)
{
    int at = argument_index(args, index);
    if (at < 0)
        return (VARIANT *)&empty_variant;
    VARIANT *value = &args->Values[at];
    if (value->vt == (VT_BYREF | VT_VARIANT) && value->pvarVal)
        return value->pvarVal;
    return value;
}

BOOL AutoPresent(AUTO_ARGS *args, UINT index)
{
    int at = argument_index(args, index);
    if (at < 0)
        return FALSE;
    const VARIANT *value = &args->Values[at];
    if (!value->vt)
        return FALSE;
    if (value->vt == (VT_BYREF | VT_VARIANT))
        return value->pvarVal && value->pvarVal->vt;
    return TRUE;
}

HRESULT AutoGetInt(const VARIANT *value, int *out)
{
    switch (value->vt) {
    case VT_I2:
        *out = value->iVal;
        return S_OK;
    case VT_I4:
        *out = value->lVal;
        return S_OK;
    case VT_R8:
        return VarI4FromR8(value->dblVal, (LONG *)out) < 0 ? DISP_E_TYPEMISMATCH : S_OK;
    case VT_BOOL:
        *out = value->boolVal != 0;
        return S_OK;
    case VT_BYREF | VT_I2:
        *out = *(SHORT *)value->byref;
        return S_OK;
    case VT_BYREF | VT_I4:
        *out = *(LONG *)value->byref;
        return S_OK;
    case VT_BYREF | VT_R8:
        return VarI4FromR8(*(DOUBLE *)value->byref, (LONG *)out) < 0 ? DISP_E_TYPEMISMATCH : S_OK;
    case VT_BYREF | VT_BOOL:
        *out = *(VARIANT_BOOL *)value->byref != 0;
        return S_OK;
    }
    return DISP_E_TYPEMISMATCH;
}

HRESULT AutoGetString(const VARIANT *value, PCWSTR *out)
{
    switch (value->vt) {
    case VT_BSTR:
        *out = value->bstrVal;
        return S_OK;
    case VT_BYREF | VT_BSTR:
        *out = *value->pbstrVal;
        return S_OK;
    case VT_EMPTY:
        *out = nullptr;
        break;
    }
    return DISP_E_TYPEMISMATCH;
}

HRESULT AutoGetBool(const VARIANT *value, BOOL *out)
{
    int number;
    HRESULT hr = AutoGetInt(value, &number);
    if (!hr)
        *out = number != 0;
    return hr;
}

BOOL AutoIsNumeric(const VARIANT *value)
{
    BYTE type = (BYTE)value->vt;
    return type >= VT_I2 && type <= VT_R8;
}

/* the handle of a record or a database object: zero when the argument is no object, -1 when it is the wrong one. */
MSIHANDLE AutoGetHandle(const VARIANT *value, const GUID *iid)
{
    IUnknown *unknown;
    switch (value->vt) {
    case VT_DISPATCH:
    case VT_UNKNOWN:
        unknown = value->punkVal;
        break;
    case VT_BYREF | VT_DISPATCH:
    case VT_BYREF | VT_UNKNOWN:
        unknown = value->ppunkVal ? *value->ppunkVal : nullptr;
        break;
    default:
        return 0;
    }
    if (!unknown)
        return 0;
    void *probe;
    if (unknown->lpVtbl->QueryInterface(unknown, iid, &probe))
        return (MSIHANDLE)-1;
    ((IUnknown *)probe)->lpVtbl->Release((IUnknown *)probe);
    IDispatch *dispatch;
    if (unknown->lpVtbl->QueryInterface(unknown, &IID_IDispatch, (void **)&dispatch))
        return (MSIHANDLE)-1;
    if (!IsEqualGUID(iid, &IID_AutoRecord) && !IsEqualGUID(iid, &IID_AutoDatabase)) {
        dispatch->lpVtbl->Release(dispatch);
        return (MSIHANDLE)-1;
    }
    DISPPARAMS none = {};
    VARIANT result = {};
    HRESULT hr = dispatch->lpVtbl->Invoke(dispatch, AUTO_ID_HANDLE, &GUID_NULL, 0, DISPATCH_PROPERTYGET, &none, &result,
                                          nullptr, nullptr);
    dispatch->lpVtbl->Release(dispatch);
    return hr ? (MSIHANDLE)-1 : (MSIHANDLE)result.lVal;
}

/* ------------------------------------------------------------------ results */

HRESULT AutoSetInt(AUTO_ARGS *args, int value)
{
    if (args->Result) {
        args->Result->vt = VT_I4;
        args->Result->lVal = value;
    }
    return S_OK;
}

HRESULT AutoSetBool(AUTO_ARGS *args, BOOL value)
{
    if (args->Result) {
        args->Result->vt = VT_BOOL;
        args->Result->boolVal = value ? VARIANT_TRUE : VARIANT_FALSE;
    }
    return S_OK;
}

HRESULT AutoSetDate(AUTO_ARGS *args, double value)
{
    if (args->Result) {
        args->Result->vt = VT_DATE;
        args->Result->dblVal = value;
    }
    return S_OK;
}

HRESULT AutoSetString(AUTO_ARGS *args, PCWSTR value)
{
    if (args->Result) {
        args->Result->vt = VT_BSTR;
        args->Result->bstrVal = SysAllocString(value);
    }
    return S_OK;
}

HRESULT AutoSetBstr(AUTO_ARGS *args, BSTR value)
{
    if (args->Result) {
        args->Result->vt = VT_BSTR;
        args->Result->bstrVal = value;
    } else {
        SysFreeString(value);
    }
    return S_OK;
}

HRESULT AutoSetObject(AUTO_ARGS *args, AUTO_OBJECT *object)
{
    if (args->Result) {
        args->Result->vt = VT_DISPATCH;
        args->Result->pdispVal = (IDispatch *)object;
    } else if (object) {
        AutoRelease(object);
    }
    return S_OK;
}

/* a result that stays empty, as a script sees nothing. */
HRESULT AutoSetEmpty(AUTO_ARGS *args)
{
    if (args->Result)
        args->Result->vt = VT_EMPTY;
    return S_OK;
}

/* ------------------------------------------------------------------ strings of unknown length */

void AutoTextBegin(AUTO_TEXT *text, DWORD size)
{
    text->Buffer = text->Stack;
    text->Size = size ? size : 1;
    if (text->Size > RTL_NUMBER_OF(text->Stack)) {
        text->Buffer = MimeMsiAllocate(text->Size * sizeof(WCHAR));
        if (!text->Buffer) {
            text->Buffer = text->Stack;
            text->Size = RTL_NUMBER_OF(text->Stack);
        }
    }
    text->Capacity = text->Size;
    text->Buffer[0] = 0;
}

/* the size the installer reported is the length without the terminator. */
BOOL AutoTextGrow(AUTO_TEXT *text)
{
    DWORD size = text->Size + 1;
    /* a size that asks for no more room than there is means the function did not say how much it needs. */
    if (size > 0x1000000 || size <= text->Capacity)
        return FALSE;
    PWSTR buffer = MimeMsiAllocate((SIZE_T)size * sizeof(WCHAR));
    if (!buffer)
        return FALSE;
    if (text->Buffer != text->Stack)
        MimeMsiFree(text->Buffer);
    text->Buffer = buffer;
    text->Size = text->Capacity = size;
    return TRUE;
}

void AutoTextReset(AUTO_TEXT *text)
{
    text->Size = text->Capacity;
}

void AutoTextEnd(AUTO_TEXT *text)
{
    if (text->Buffer != text->Stack)
        MimeMsiFree(text->Buffer);
}
