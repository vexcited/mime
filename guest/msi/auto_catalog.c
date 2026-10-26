/*
 * the products, patches and components of the installed state, the lists the installer object makes of them, and the
 * enumerators the scripts walk those lists with.
 */
#include "automation.h"
#include <mime/win32/winnls.h>

#define ENTRY(id, help, flags, method, names) {id, help, flags, method, names}
#define GET DISPATCH_PROPERTYGET
#define PUT DISPATCH_PROPERTYPUT
#define CALL DISPATCH_METHOD
#define COUNT_OF(table) ((ULONG)(sizeof(table) / sizeof((table)[0])))

/* ------------------------------------------------------------------ lists and their enumerators */

struct AUTO_LIST {
    LONG References;
    BOOL Objects;
    ULONG Count, Capacity;
    VARIANT *Items;
};

typedef struct AUTO_ENUM {
    const IEnumVARIANTVtbl *lpVtbl;
    LONG References;
    AUTO_LIST *List;
    ULONG Cursor;
} AUTO_ENUM;

typedef struct AUTO_COLLECTION {
    AUTO_OBJECT Base;
    AUTO_LIST *List;
    AUTO_ENUM *Enumerator;
} AUTO_COLLECTION;

AUTO_LIST *AutoListCreate(BOOL objects)
{
    AUTO_LIST *list = MimeMsiAllocate(sizeof(*list));
    if (list) {
        list->References = 1;
        list->Objects = objects;
    }
    return list;
}

static BOOL list_reserve(AUTO_LIST *list)
{
    if (list->Count < list->Capacity)
        return TRUE;
    ULONG capacity = list->Capacity ? list->Capacity * 2 : 16;
    VARIANT *items = MimeMsiAllocate((SIZE_T)capacity * sizeof(VARIANT));
    if (!items)
        return FALSE;
    if (list->Count)
        memcpy(items, list->Items, (SIZE_T)list->Count * sizeof(VARIANT));
    MimeMsiFree(list->Items);
    list->Items = items;
    list->Capacity = capacity;
    return TRUE;
}

BOOL AutoListAddString(AUTO_LIST *list, PCWSTR text, size_t length)
{
    if (!list_reserve(list))
        return FALSE;
    BSTR copy = SysAllocStringLen(text, (UINT)length);
    if (!copy)
        return FALSE;
    VARIANT *item = &list->Items[list->Count++];
    item->vt = VT_BSTR;
    item->bstrVal = copy;
    return TRUE;
}

/* the list takes the reference of the object it is given when it returns true. */
BOOL AutoListAddObject(AUTO_LIST *list, AUTO_OBJECT *object)
{
    if (!list_reserve(list))
        return FALSE;
    VARIANT *item = &list->Items[list->Count++];
    item->vt = VT_DISPATCH;
    item->pdispVal = (IDispatch *)object;
    return TRUE;
}

static void list_release(AUTO_LIST *list)
{
    if (InterlockedDecrement(&list->References))
        return;
    for (ULONG i = 0; i < list->Count; i++)
        VariantClear(&list->Items[i]);
    MimeMsiFree(list->Items);
    MimeMsiFree(list);
}

void AutoListDiscard(AUTO_LIST *list)
{
    list_release(list);
}

/* one element of a list for a script, which owns what it is given. */
static HRESULT list_element(const AUTO_LIST *list, ULONG index, VARIANT *out)
{
    const VARIANT *item = &list->Items[index];
    out->vt = item->vt;
    if (item->vt == VT_BSTR) {
        out->bstrVal = SysAllocStringLen(item->bstrVal, SysStringLen(item->bstrVal));
        if (!out->bstrVal) {
            out->vt = VT_EMPTY;
            return E_OUTOFMEMORY;
        }
    } else {
        out->pdispVal = item->pdispVal;
        item->pdispVal->lpVtbl->AddRef(item->pdispVal);
    }
    return S_OK;
}

static HRESULT WINAPI enum_query_interface(IEnumVARIANT *self, REFIID iid, void **out)
{
    if (IsEqualGUID(iid, &IID_IEnumVARIANT) || IsEqualGUID(iid, &IID_IUnknown)) {
        *out = self;
        self->lpVtbl->AddRef(self);
        return S_OK;
    }
    *out = nullptr;
    return E_NOINTERFACE;
}

static ULONG WINAPI enum_add_ref(IEnumVARIANT *self)
{
    return (ULONG)InterlockedIncrement(&((AUTO_ENUM *)self)->References);
}

static ULONG WINAPI enum_release(IEnumVARIANT *self)
{
    AUTO_ENUM *enumerator = (AUTO_ENUM *)self;
    LONG references = InterlockedDecrement(&enumerator->References);
    if (!references) {
        list_release(enumerator->List);
        MimeMsiFree(enumerator);
        InterlockedDecrement(&MimeMsiAutoInstances);
    }
    return (ULONG)references;
}

static HRESULT WINAPI enum_next(IEnumVARIANT *self, ULONG count, VARIANT *values, ULONG *fetched)
{
    AUTO_ENUM *enumerator = (AUTO_ENUM *)self;
    if (fetched)
        *fetched = 0;
    if (!values)
        return S_FALSE;
    while (count--) {
        if (enumerator->Cursor >= enumerator->List->Count)
            return S_FALSE;
        HRESULT hr = list_element(enumerator->List, enumerator->Cursor, values);
        if (hr)
            return hr;
        enumerator->Cursor++;
        values++;
        if (fetched)
            ++*fetched;
    }
    return S_OK;
}

static HRESULT WINAPI enum_skip(IEnumVARIANT *self, ULONG count)
{
    AUTO_ENUM *enumerator = (AUTO_ENUM *)self;
    while (count--) {
        if (enumerator->Cursor >= enumerator->List->Count)
            return S_FALSE;
        enumerator->Cursor++;
    }
    return S_OK;
}

static HRESULT WINAPI enum_reset(IEnumVARIANT *self)
{
    ((AUTO_ENUM *)self)->Cursor = 0;
    return S_OK;
}

static AUTO_ENUM *enum_create(AUTO_LIST *list);

/* a copy starts again from the first element. */
static HRESULT WINAPI enum_clone(IEnumVARIANT *self, IEnumVARIANT **out)
{
    AUTO_ENUM *copy = enum_create(((AUTO_ENUM *)self)->List);
    if (!copy)
        return E_OUTOFMEMORY;
    *out = (IEnumVARIANT *)copy;
    return S_OK;
}

static const IEnumVARIANTVtbl enum_vtbl = {
    enum_query_interface, enum_add_ref, enum_release, enum_next, enum_skip, enum_reset, enum_clone,
};

static AUTO_ENUM *enum_create(AUTO_LIST *list)
{
    AUTO_ENUM *enumerator = MimeMsiAllocate(sizeof(*enumerator));
    if (!enumerator)
        return nullptr;
    enumerator->lpVtbl = &enum_vtbl;
    enumerator->References = 1;
    enumerator->List = list;
    InterlockedIncrement(&list->References);
    InterlockedIncrement(&MimeMsiAutoInstances);
    return enumerator;
}

/* ------------------------------------------------------------------ the collection object */

static HRESULT collection_new_enum(AUTO_OBJECT *object, AUTO_ARGS *args)
{
    AUTO_ENUM *enumerator = ((AUTO_COLLECTION *)object)->Enumerator;
    enumerator->lpVtbl->AddRef((IEnumVARIANT *)enumerator);
    if (args->Result) {
        args->Result->vt = VT_UNKNOWN;
        args->Result->punkVal = (IUnknown *)enumerator;
    } else {
        enumerator->lpVtbl->Release((IEnumVARIANT *)enumerator);
    }
    return S_OK;
}

/* an index out of range is a bad index, and a call with nowhere to put the result an object list refuses. */
static HRESULT collection_item(AUTO_OBJECT *object, AUTO_ARGS *args)
{
    AUTO_LIST *list = ((AUTO_COLLECTION *)object)->List;
    AUTO_INT_ARG(args, 1, index);
    if (!args->Result)
        return list->Objects ? S_FALSE : S_OK;
    if ((ULONG)index >= list->Count)
        return DISP_E_BADINDEX;
    return list_element(list, (ULONG)index, args->Result);
}

static HRESULT collection_count(AUTO_OBJECT *object, AUTO_ARGS *args)
{
    return AutoSetInt(args, (int)((AUTO_COLLECTION *)object)->List->Count);
}

static const AUTO_ENTRY collection_entries[] = {
    ENTRY(-4, 0, CALL, collection_new_enum, L"_NewEnum"),
    ENTRY(0, 0, GET, collection_item, L"Item,Index"),
    ENTRY(1, 0, GET, collection_count, L"Count"),
};

static void collection_destroy(AUTO_OBJECT *object)
{
    AUTO_COLLECTION *collection = (AUTO_COLLECTION *)object;
    collection->Enumerator->lpVtbl->Release((IEnumVARIANT *)collection->Enumerator);
    list_release(collection->List);
    MimeMsiFree(collection);
}

/* the list belongs to the collection from here on. */
HRESULT AutoListReturn(AUTO_ARGS *args, AUTO_LIST *list)
{
    AUTO_COLLECTION *collection = (AUTO_COLLECTION *)AutoAllocate(
        sizeof(*collection), collection_entries, COUNT_OF(collection_entries),
        list->Objects ? &IID_AutoRecordList : &IID_AutoStringList, 0, collection_destroy);
    if (!collection) {
        list_release(list);
        return E_OUTOFMEMORY;
    }
    collection->List = list;
    collection->Enumerator = enum_create(list);
    if (!collection->Enumerator) {
        AutoRelease(&collection->Base);
        return E_OUTOFMEMORY;
    }
    return AutoSetObject(args, &collection->Base);
}

/* ------------------------------------------------------------------ enumerations of strings */

/* an error of an enumeration as the scripts see it: a Win32 error becomes an HRESULT of the system facility. */
static HRESULT enum_error(UINT error)
{
    return AutoWin32ToHresult(error);
}

HRESULT AutoEnumerateStrings(AUTO_ARGS *args, ULONG kind, PCWSTR first, PCWSTR second, PCWSTR sid, DWORD context,
                             DWORD source_type)
{
    AUTO_LIST *list = AutoListCreate(FALSE);
    if (!list)
        return E_OUTOFMEMORY;
    MSIHANDLE *patch_files = nullptr;
    DWORD patch_file_count = 0;
    UINT error = ERROR_SUCCESS;
    AUTO_TEXT text;
    AutoTextBegin(&text, 1025);
    for (DWORD index = 0;; index++) {
        WCHAR code[39], transforms[128];
        PCWSTR value = code;
        size_t length;
        AutoTextReset(&text);
        code[0] = 0;
        switch (kind) {
        case AUTO_ENUM_QUALIFIERS:
            do
                error = AutoApiEnumComponentQualifiers(first, index, text.Buffer, &text.Size);
            while (error == ERROR_MORE_DATA && AutoTextGrow(&text));
            value = text.Buffer;
            break;
        case AUTO_ENUM_PRODUCTS:
            error = MsiEnumProductsW(index, code);
            break;
        case AUTO_ENUM_FEATURES:
            error = MsiEnumFeaturesW(first, index, code, nullptr);
            break;
        case AUTO_ENUM_COMPONENTS:
            error = MsiEnumComponentsW(index, code);
            break;
        case AUTO_ENUM_CLIENTS:
            error = MsiEnumClientsW(first, index, code);
            break;
        case AUTO_ENUM_PATCHES: {
            DWORD size = RTL_NUMBER_OF(transforms);
            error = MsiEnumPatchesW(first, index, code, transforms, &size);
            break;
        }
        case AUTO_ENUM_RELATED:
            error = MsiEnumRelatedProductsW(first, 0, index, code);
            break;
        case AUTO_ENUM_SOURCES:
            do
                error = MsiSourceListEnumSourcesW(first, sid, (MSIINSTALLCONTEXT)context, source_type, index,
                                                  text.Buffer, &text.Size);
            while (error == ERROR_MORE_DATA && AutoTextGrow(&text));
            value = text.Buffer;
            break;
        case AUTO_ENUM_PATCH_FILES:
            if (!index) {
                error = AutoApiGetPatchFileList(first, second, &patch_file_count, &patch_files);
                if (error)
                    break;
            }
            if (index >= patch_file_count) {
                error = ERROR_NO_MORE_ITEMS;
                break;
            }
            AutoTextReset(&text);
            do
                error = MsiRecordGetStringW(patch_files[index], 0, text.Buffer, &text.Size);
            while (error == ERROR_MORE_DATA && AutoTextGrow(&text));
            value = text.Buffer;
            break;
        default:
            error = ERROR_INVALID_PARAMETER;
        }
        /* an entry that does not fit its buffer is skipped. */
        if (error == ERROR_MORE_DATA && kind == AUTO_ENUM_FEATURES)
            continue;
        if (error)
            break;
        length = wcslen(value);
        if (!AutoListAddString(list, value, length)) {
            error = ERROR_OUTOFMEMORY;
            break;
        }
    }
    AutoTextEnd(&text);
    if (patch_files) {
        for (DWORD i = 0; i < patch_file_count; i++)
            MsiCloseHandle(patch_files[i]);
        LocalFree(patch_files);
    }
    if (error != ERROR_NO_MORE_ITEMS) {
        AutoListDiscard(list);
        return error == ERROR_OUTOFMEMORY ? E_OUTOFMEMORY : enum_error(error);
    }
    return AutoListReturn(args, list);
}

/* ------------------------------------------------------------------ products */

typedef struct AUTO_PRODUCT {
    AUTO_OBJECT Base;
    DWORD Context;
    int State;
    WCHAR Code[39];
    PWSTR Sid;
} AUTO_PRODUCT;

static void product_destroy(AUTO_OBJECT *object)
{
    MimeMsiFree(((AUTO_PRODUCT *)object)->Sid);
    MimeMsiFree(object);
}

static HRESULT product_text_value(AUTO_ARGS *args, PCWSTR value)
{
    return AutoSetString(args, value ? value : L"");
}

static HRESULT product_code(AUTO_OBJECT *object, AUTO_ARGS *args)
{
    return product_text_value(args, ((AUTO_PRODUCT *)object)->Code);
}

static HRESULT product_user_sid(AUTO_OBJECT *object, AUTO_ARGS *args)
{
    return product_text_value(args, ((AUTO_PRODUCT *)object)->Sid);
}

static HRESULT product_context(AUTO_OBJECT *object, AUTO_ARGS *args)
{
    return AutoSetInt(args, (int)((AUTO_PRODUCT *)object)->Context);
}

/* a product is installed when its installed name reads, advertised when only its name does. */
static HRESULT product_state(AUTO_OBJECT *object, AUTO_ARGS *args)
{
    AUTO_PRODUCT *product = (AUTO_PRODUCT *)object;
    if (product->State == -2) {
        DWORD size = 0;
        product->State = -1;
        if (MsiGetProductInfoExW(product->Code, product->Sid, (MSIINSTALLCONTEXT)product->Context, L"InstalledProductName", nullptr,
                                    &size)) {
            size = 0;
            if (!MsiGetProductInfoExW(product->Code, product->Sid, (MSIINSTALLCONTEXT)product->Context, L"ProductName", nullptr,
                                         &size))
                product->State = 1;
        } else {
            product->State = 5;
        }
    }
    return AutoSetInt(args, product->State);
}

static HRESULT product_install_property(AUTO_OBJECT *object, AUTO_ARGS *args)
{
    AUTO_PRODUCT *product = (AUTO_PRODUCT *)object;
    AUTO_STRING_ARG(args, 1, property);
    AUTO_TEXT text;
    AutoTextBegin(&text, 100);
    UINT error;
    do
        error = MsiGetProductInfoExW(product->Code, product->Sid, (MSIINSTALLCONTEXT)product->Context, property, text.Buffer,
                                        &text.Size);
    while (error == ERROR_MORE_DATA && AutoTextGrow(&text));
    HRESULT hr = error ? (HRESULT)error : AutoSetString(args, text.Buffer);
    AutoTextEnd(&text);
    return hr;
}

static HRESULT product_component_state(AUTO_OBJECT *object, AUTO_ARGS *args)
{
    AUTO_PRODUCT *product = (AUTO_PRODUCT *)object;
    AUTO_STRING_ARG(args, 1, component);
    INSTALLSTATE state = INSTALLSTATE_BROKEN;
    UINT error = MsiQueryComponentStateW(product->Code, product->Sid, (MSIINSTALLCONTEXT)product->Context, component, &state);
    return error ? (HRESULT)error : AutoSetInt(args, state);
}

static HRESULT product_feature_state(AUTO_OBJECT *object, AUTO_ARGS *args)
{
    AUTO_PRODUCT *product = (AUTO_PRODUCT *)object;
    AUTO_STRING_ARG(args, 1, feature);
    INSTALLSTATE state = INSTALLSTATE_BROKEN;
    UINT error = MsiQueryFeatureStateExW(product->Code, product->Sid, (MSIINSTALLCONTEXT)product->Context, feature, &state);
    return error ? (HRESULT)error : AutoSetInt(args, state);
}

/* the source list of a product or a patch; a patch is named by its own code with the patch bit of the options. */
typedef struct AUTO_SOURCES {
    PCWSTR Code, Sid;
    DWORD Context, Extra;
} AUTO_SOURCES;

static AUTO_SOURCES sources_of(AUTO_OBJECT *object, BOOL patch);

static HRESULT source_add(AUTO_OBJECT *object, AUTO_ARGS *args, BOOL patch)
{
    AUTO_SOURCES s = sources_of(object, patch);
    AUTO_INT_ARG(args, 1, type);
    AUTO_STRING_ARG(args, 2, source);
    AUTO_INT_ARG(args, 3, index);
    return enum_error(MsiSourceListAddSourceExW(s.Code, s.Sid, (MSIINSTALLCONTEXT)s.Context, (DWORD)type | s.Extra,
                                                source, (DWORD)index));
}

static HRESULT source_add_disk(AUTO_OBJECT *object, AUTO_ARGS *args, BOOL patch)
{
    AUTO_SOURCES s = sources_of(object, patch);
    AUTO_INT_ARG(args, 1, disk);
    AUTO_STRING_ARG(args, 2, label);
    AUTO_STRING_ARG(args, 3, prompt);
    return enum_error(AutoApiSourceListAddMediaDisk(s.Code, s.Sid, s.Context, s.Extra, (DWORD)disk, label, prompt));
}

static HRESULT source_clear(AUTO_OBJECT *object, AUTO_ARGS *args, BOOL patch)
{
    AUTO_SOURCES s = sources_of(object, patch);
    AUTO_INT_ARG(args, 1, type);
    AUTO_STRING_ARG(args, 2, source);
    return enum_error(AutoApiSourceListClearSource(s.Code, s.Sid, s.Context, (DWORD)type | s.Extra, source));
}

static HRESULT source_clear_disk(AUTO_OBJECT *object, AUTO_ARGS *args, BOOL patch)
{
    AUTO_SOURCES s = sources_of(object, patch);
    AUTO_INT_ARG(args, 1, disk);
    return enum_error(AutoApiSourceListClearMediaDisk(s.Code, s.Sid, s.Context, s.Extra, (DWORD)disk));
}

static HRESULT source_clear_all(AUTO_OBJECT *object, AUTO_ARGS *args, BOOL patch)
{
    AUTO_SOURCES s = sources_of(object, patch);
    AUTO_INT_ARG(args, 1, type);
    return enum_error(AutoApiSourceListClearAllEx(s.Code, s.Sid, s.Context, (DWORD)type | s.Extra));
}

static HRESULT source_force(AUTO_OBJECT *object, AUTO_ARGS *args, BOOL patch)
{
    AUTO_SOURCES s = sources_of(object, patch);
    return enum_error(AutoApiSourceListForceResolutionEx(s.Code, s.Sid, s.Context, s.Extra));
}

/* reads or sets a property of the source list; LastUsedSource sets with a prefix naming the kind of source. */
static HRESULT source_info(AUTO_OBJECT *object, AUTO_ARGS *args, BOOL patch)
{
    AUTO_SOURCES s = sources_of(object, patch);
    AUTO_STRING_ARG(args, 1, property);
    if (args->Flags & PUT) {
        VARIANT *variant = AutoArgument(args, 0);
        if (!variant->vt)
            return S_OK;
        PCWSTR value = nullptr;
        HRESULT hr = AutoGetString(variant, &value);
        if (hr)
            return hr;
        DWORD options = s.Extra;
        if (CompareStringW(0x409, NORM_IGNORECASE, property, -1, L"LastUsedSource", -1) == CSTR_EQUAL) {
            size_t length = value ? wcslen(value) : 0;
            WCHAR kind;
            if (length < 3 || value[1] != L';')
                return E_FAIL;
            kind = value[0] & 0x5F;
            if (kind == L'N')
                options |= MSISOURCETYPE_NETWORK;
            else if (kind == L'U')
                options |= MSISOURCETYPE_URL;
            else
                return E_FAIL;
            value += 2;
        }
        return enum_error(MsiSourceListSetInfoW(s.Code, s.Sid, (MSIINSTALLCONTEXT)s.Context, options, property,
                                                value));
    }
    AUTO_TEXT text;
    AutoTextBegin(&text, 256);
    UINT error;
    do
        error = MsiSourceListGetInfoW(s.Code, s.Sid, (MSIINSTALLCONTEXT)s.Context, s.Extra, property, text.Buffer,
                                      &text.Size);
    while (error == ERROR_MORE_DATA && AutoTextGrow(&text));
    HRESULT hr = error ? enum_error(error) : AutoSetString(args, text.Buffer);
    AutoTextEnd(&text);
    return hr;
}

static HRESULT source_list(AUTO_OBJECT *object, AUTO_ARGS *args, BOOL patch)
{
    AUTO_SOURCES s = sources_of(object, patch);
    AUTO_INT_ARG(args, 1, type);
    return AutoEnumerateStrings(args, AUTO_ENUM_SOURCES, s.Code, nullptr, s.Sid, s.Context, (DWORD)type | s.Extra);
}

static HRESULT source_disks(AUTO_OBJECT *object, AUTO_ARGS *args, BOOL patch)
{
    AUTO_SOURCES s = sources_of(object, patch);
    AUTO_LIST *list = AutoListCreate(TRUE);
    if (!list)
        return E_OUTOFMEMORY;
    UINT error;
    for (DWORD index = 0;; index++) {
        WCHAR label[64], prompt[256];
        DWORD disk = 0, label_size = RTL_NUMBER_OF(label), prompt_size = RTL_NUMBER_OF(prompt);
        label[0] = prompt[0] = 0;
        error = AutoApiSourceListEnumMediaDisks(s.Code, s.Sid, s.Context, s.Extra, index, &disk, label, &label_size,
                                                prompt, &prompt_size);
        if (error)
            break;
        MSIHANDLE record = MsiCreateRecord(3);
        if (!record || MsiRecordSetInteger(record, 1, (int)disk) || MsiRecordSetStringW(record, 2, label) ||
            MsiRecordSetStringW(record, 3, prompt)) {
            if (record)
                MsiCloseHandle(record);
            error = ERROR_OUTOFMEMORY;
            break;
        }
        AUTO_OBJECT *item = AutoNewRecord(record);
        if (!item || !AutoListAddObject(list, item)) {
            if (item)
                AutoRelease(item);
            error = ERROR_OUTOFMEMORY;
            break;
        }
    }
    if (error != ERROR_NO_MORE_ITEMS) {
        AutoListDiscard(list);
        return error == ERROR_OUTOFMEMORY ? E_OUTOFMEMORY : enum_error(error);
    }
    return AutoListReturn(args, list);
}

#define PRODUCT_SOURCE(name, fn) \
    static HRESULT product_##name(AUTO_OBJECT *object, AUTO_ARGS *args) { return fn(object, args, FALSE); }
PRODUCT_SOURCE(source_add, source_add)
PRODUCT_SOURCE(source_add_disk, source_add_disk)
PRODUCT_SOURCE(source_clear, source_clear)
PRODUCT_SOURCE(source_clear_disk, source_clear_disk)
PRODUCT_SOURCE(source_clear_all, source_clear_all)
PRODUCT_SOURCE(source_force, source_force)
PRODUCT_SOURCE(source_info, source_info)
PRODUCT_SOURCE(sources, source_list)
PRODUCT_SOURCE(media_disks, source_disks)

static const AUTO_ENTRY product_entries[] = {
    ENTRY(1, 9111, GET, product_code, L"ProductCode"),
    ENTRY(2, 9112, GET, product_user_sid, L"UserSid"),
    ENTRY(3, 9113, GET, product_context, L"Context"),
    ENTRY(4, 9114, GET, product_state, L"State"),
    ENTRY(5, 9115, GET, product_install_property, L"InstallProperty"),
    ENTRY(6, 9116, GET, product_component_state, L"ComponentState"),
    ENTRY(7, 9117, GET, product_feature_state, L"FeatureState"),
    ENTRY(8, 9118, CALL, product_source_add, L"SourceListAddSource,SourceType,Source,dwIndex"),
    ENTRY(9, 9119, CALL, product_source_add_disk, L"SourceListAddMediaDisk,dwDiskId,VolumeLabel,DiskPrompt"),
    ENTRY(10, 9120, CALL, product_source_clear, L"SourceListClearSource,SourceType,szSource"),
    ENTRY(11, 9121, CALL, product_source_clear_disk, L"SourceListClearMediaDisk,dwDiskId"),
    ENTRY(12, 9122, CALL, product_source_clear_all, L"SourceListClearAll,SourceType"),
    ENTRY(13, 9123, CALL, product_source_force, L"SourceListForceResolution"),
    ENTRY(14, 9124, GET, product_sources, L"Sources,SourceType"),
    ENTRY(15, 9125, CALL, product_media_disks, L"MediaDisks"),
    ENTRY(16, 9126, GET | PUT, product_source_info, L"SourceListInfo,Property,SourceType"),
};

AUTO_OBJECT *AutoNewProduct(PCWSTR product, PCWSTR sid, DWORD context)
{
    if (!product || wcslen(product) > 38)
        return nullptr;
    AUTO_PRODUCT *object = (AUTO_PRODUCT *)AutoAllocate(sizeof(AUTO_PRODUCT), product_entries,
                                                        COUNT_OF(product_entries), &IID_IUnknown, 0, product_destroy);
    if (!object)
        return nullptr;
    object->State = -2;
    object->Context = context;
    wcscpy(object->Code, product);
    if (sid && *sid) {
        object->Sid = MimeMsiDuplicate(sid);
        if (!object->Sid) {
            AutoRelease(&object->Base);
            return nullptr;
        }
    }
    return &object->Base;
}

/* ------------------------------------------------------------------ patches */

typedef struct AUTO_PATCH {
    AUTO_OBJECT Base;
    DWORD Context;
    WCHAR Code[39], Product[39];
    PWSTR Sid;
    int State;
} AUTO_PATCH;

static void patch_destroy(AUTO_OBJECT *object)
{
    MimeMsiFree(((AUTO_PATCH *)object)->Sid);
    MimeMsiFree(object);
}

static AUTO_SOURCES sources_of(AUTO_OBJECT *object, BOOL patch)
{
    AUTO_SOURCES s;
    if (patch) {
        AUTO_PATCH *item = (AUTO_PATCH *)object;
        s = (AUTO_SOURCES){item->Code, item->Sid, item->Context, MSICODE_PATCH};
    } else {
        AUTO_PRODUCT *item = (AUTO_PRODUCT *)object;
        s = (AUTO_SOURCES){item->Code, item->Sid, item->Context, 0};
    }
    return s;
}

static HRESULT patch_code(AUTO_OBJECT *object, AUTO_ARGS *args)
{
    return product_text_value(args, ((AUTO_PATCH *)object)->Code);
}

static HRESULT patch_product_code(AUTO_OBJECT *object, AUTO_ARGS *args)
{
    return product_text_value(args, ((AUTO_PATCH *)object)->Product);
}

static HRESULT patch_user_sid(AUTO_OBJECT *object, AUTO_ARGS *args)
{
    return product_text_value(args, ((AUTO_PATCH *)object)->Sid);
}

static HRESULT patch_context(AUTO_OBJECT *object, AUTO_ARGS *args)
{
    return AutoSetInt(args, (int)((AUTO_PATCH *)object)->Context);
}

static HRESULT patch_info(AUTO_OBJECT *object, PCWSTR property, AUTO_TEXT *text, UINT *error)
{
    AUTO_PATCH *patch = (AUTO_PATCH *)object;
    do
        *error = MsiGetPatchInfoExW(patch->Code, patch->Product, patch->Sid, (MSIINSTALLCONTEXT)patch->Context,
                                    property, text->Buffer, &text->Size);
    while (*error == ERROR_MORE_DATA && AutoTextGrow(text));
    return S_OK;
}

static HRESULT patch_state(AUTO_OBJECT *object, AUTO_ARGS *args)
{
    AUTO_PATCH *patch = (AUTO_PATCH *)object;
    if (!patch->Product[0])
        return AutoWin32ToHresult(ERROR_UNKNOWN_PATCH);
    AUTO_TEXT text;
    AutoTextBegin(&text, 4);
    UINT error;
    patch_info(object, L"State", &text, &error);
    HRESULT hr;
    if (error) {
        hr = enum_error(error);
    } else {
        patch->State = 0;
        for (PCWSTR digit = text.Buffer; *digit >= L'0' && *digit <= L'9'; digit++)
            patch->State = patch->State * 10 + (*digit - L'0');
        hr = AutoSetInt(args, patch->State);
    }
    AutoTextEnd(&text);
    return hr;
}

static HRESULT patch_property(AUTO_OBJECT *object, AUTO_ARGS *args)
{
    AUTO_PATCH *patch = (AUTO_PATCH *)object;
    AUTO_STRING_ARG(args, 1, property);
    if (!patch->Product[0])
        return AutoWin32ToHresult(ERROR_UNKNOWN_PATCH);
    AUTO_TEXT text;
    AutoTextBegin(&text, 100);
    UINT error;
    patch_info(object, property, &text, &error);
    HRESULT hr = error ? enum_error(error) : AutoSetString(args, text.Buffer);
    AutoTextEnd(&text);
    return hr;
}

#define PATCH_SOURCE(name, fn) \
    static HRESULT patch_##name(AUTO_OBJECT *object, AUTO_ARGS *args) { return fn(object, args, TRUE); }
PATCH_SOURCE(source_add, source_add)
PATCH_SOURCE(source_add_disk, source_add_disk)
PATCH_SOURCE(source_clear, source_clear)
PATCH_SOURCE(source_clear_disk, source_clear_disk)
PATCH_SOURCE(source_clear_all, source_clear_all)
PATCH_SOURCE(source_force, source_force)
PATCH_SOURCE(source_info, source_info)
PATCH_SOURCE(sources, source_list)
PATCH_SOURCE(media_disks, source_disks)

static const AUTO_ENTRY patch_entries[] = {
    ENTRY(1, 9131, GET, patch_code, L"PatchCode"),
    ENTRY(2, 9132, GET, patch_product_code, L"ProductCode"),
    ENTRY(3, 9133, GET, patch_user_sid, L"UserSid"),
    ENTRY(4, 9134, GET, patch_context, L"Context"),
    ENTRY(5, 9135, GET, patch_state, L"State"),
    ENTRY(6, 9136, CALL, patch_source_add, L"SourceListAddSource,SourceType,Source,dwIndex"),
    ENTRY(7, 9137, CALL, patch_source_add_disk, L"SourceListAddMediaDisk,dwDiskId,VolumeLabel,DiskPrompt"),
    ENTRY(8, 9138, CALL, patch_source_clear, L"SourceListClearSource,SourceType,szSource"),
    ENTRY(9, 9139, CALL, patch_source_clear_disk, L"SourceListClearMediaDisk,dwDiskId"),
    ENTRY(10, 9140, CALL, patch_source_clear_all, L"SourceListClearAll,SourceType"),
    ENTRY(11, 9141, CALL, patch_source_force, L"SourceListForceResolution"),
    ENTRY(12, 9142, GET, patch_sources, L"Sources,SourceType"),
    ENTRY(13, 9143, CALL, patch_media_disks, L"MediaDisks"),
    ENTRY(14, 9144, GET | PUT, patch_source_info, L"SourceListInfo,Property,SourceType"),
    ENTRY(15, 9145, GET, patch_property, L"PatchProperty,Property"),
};

AUTO_OBJECT *AutoNewPatch(PCWSTR patch, PCWSTR product, PCWSTR sid, DWORD context)
{
    if (!patch || !product || wcslen(patch) > 38 || wcslen(product) > 38)
        return nullptr;
    AUTO_PATCH *object = (AUTO_PATCH *)AutoAllocate(sizeof(AUTO_PATCH), patch_entries, COUNT_OF(patch_entries),
                                                    &IID_IUnknown, 0, patch_destroy);
    if (!object)
        return nullptr;
    object->Context = context;
    wcscpy(object->Code, patch);
    wcscpy(object->Product, product);
    if (sid && *sid) {
        object->Sid = MimeMsiDuplicate(sid);
        if (!object->Sid) {
            AutoRelease(&object->Base);
            return nullptr;
        }
    }
    return &object->Base;
}

/* ------------------------------------------------------------------ components */

typedef struct AUTO_COMPONENT {
    AUTO_OBJECT Base;
    DWORD Context;
    WCHAR Code[39], Product[39];
    PWSTR Sid;
} AUTO_COMPONENT;

static void component_destroy(AUTO_OBJECT *object)
{
    MimeMsiFree(((AUTO_COMPONENT *)object)->Sid);
    MimeMsiFree(object);
}

static HRESULT component_code(AUTO_OBJECT *object, AUTO_ARGS *args)
{
    return product_text_value(args, ((AUTO_COMPONENT *)object)->Code);
}

static HRESULT component_product(AUTO_OBJECT *object, AUTO_ARGS *args)
{
    return product_text_value(args, ((AUTO_COMPONENT *)object)->Product);
}

static HRESULT component_sid(AUTO_OBJECT *object, AUTO_ARGS *args)
{
    return product_text_value(args, ((AUTO_COMPONENT *)object)->Sid);
}

static HRESULT component_context(AUTO_OBJECT *object, AUTO_ARGS *args)
{
    return AutoSetInt(args, (int)((AUTO_COMPONENT *)object)->Context);
}

static const AUTO_ENTRY component_entries[] = {
    ENTRY(1, 10002, GET, component_code, L"ComponentCode"),
    ENTRY(2, 10003, GET, component_sid, L"UserSid"),
    ENTRY(3, 10004, GET, component_context, L"Context"),
};

static const AUTO_ENTRY client_entries[] = {
    ENTRY(1, 10103, GET, component_code, L"ComponentCode"),
    ENTRY(2, 10102, GET, component_product, L"ProductCode"),
    ENTRY(3, 10104, GET, component_sid, L"UserSid"),
    ENTRY(4, 10105, GET, component_context, L"Context"),
};

/* the path of a component: its code, the path as the user sid of the object and the state as its context. */
static const AUTO_ENTRY path_entries[] = {
    ENTRY(1, 10202, GET, component_code, L"ComponentCode"),
    ENTRY(2, 10203, GET, component_sid, L"Path"),
    ENTRY(3, 10204, GET, component_context, L"State"),
};

static AUTO_OBJECT *component_create(const AUTO_ENTRY *entries, ULONG count, PCWSTR component, PCWSTR product,
                                     PCWSTR sid, DWORD context)
{
    if (!component || wcslen(component) > 38 || (product && wcslen(product) > 38))
        return nullptr;
    AUTO_COMPONENT *object = (AUTO_COMPONENT *)AutoAllocate(sizeof(AUTO_COMPONENT), entries, count, &IID_IUnknown, 0,
                                                            component_destroy);
    if (!object)
        return nullptr;
    object->Context = context;
    wcscpy(object->Code, component);
    if (product)
        wcscpy(object->Product, product);
    if (sid && *sid) {
        object->Sid = MimeMsiDuplicate(sid);
        if (!object->Sid) {
            AutoRelease(&object->Base);
            return nullptr;
        }
    }
    return &object->Base;
}

AUTO_OBJECT *AutoNewComponent(PCWSTR component, PCWSTR sid, DWORD context)
{
    return component_create(component_entries, COUNT_OF(component_entries), component, nullptr, sid, context);
}

AUTO_OBJECT *AutoNewComponentClient(PCWSTR component, PCWSTR product, PCWSTR sid, DWORD context)
{
    if (!product)
        return nullptr;
    return component_create(client_entries, COUNT_OF(client_entries), component, product, sid, context);
}

AUTO_OBJECT *AutoNewComponentPath(PCWSTR component, PCWSTR path, INSTALLSTATE state)
{
    return component_create(path_entries, COUNT_OF(path_entries), component, nullptr, path, (DWORD)state);
}
