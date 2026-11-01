/*
 * the objects a script gets from the host: WScript, the arguments with their named and unnamed collections, and the errors
 * they signal. They are dual interfaces that answer IDispatch from the type library in the resources of this program.
 */
#include "wsh.h"

static PWSTR duplicate(PCWSTR text)
{
    if (!text)
        return nullptr;
    PWSTR copy = HeapAlloc(GetProcessHeap(), 0, (wcslen(text) + 1) * sizeof(WCHAR));
    if (copy)
        wcscpy(copy, text);
    return copy;
}

/* ------------------------------------------------------------------ the type library */

static ITypeLib *g_library;
static SRWLOCK g_library_lock = SRWLOCK_INIT;

HRESULT WshTypeInfoOf(const GUID *iid, ITypeInfo **info)
{
    *info = nullptr;
    AcquireSRWLockExclusive(&g_library_lock);
    HRESULT hr = S_OK;
    if (!g_library)
        hr = LoadTypeLibEx(g_host.Module, REGKIND_NONE, &g_library);
    if (SUCCEEDED(hr))
        hr = g_library->lpVtbl->GetTypeInfoOfGuid(g_library, iid, info);
    ReleaseSRWLockExclusive(&g_library_lock);
    return hr;
}

/* an error for the script that called: the interface, the source and the text. */
HRESULT WshSignalText(const GUID *iid, PCWSTR source, PCWSTR text)
{
    ICreateErrorInfo *create;
    IErrorInfo *error;
    if (SUCCEEDED(CreateErrorInfo(&create))) {
        create->lpVtbl->SetGUID(create, iid);
        create->lpVtbl->SetSource(create, (LPOLESTR)source);
        create->lpVtbl->SetDescription(create, (LPOLESTR)(text ? text : L""));
        if (SUCCEEDED(create->lpVtbl->QueryInterface(create, &IID_IErrorInfo, (void **)&error))) {
            SetErrorInfo(0, error);
            error->lpVtbl->Release(error);
        }
        create->lpVtbl->Release(create);
    }
    return DISP_E_EXCEPTION;
}

/* the same with a message of the string table. */
HRESULT WshSignal(const GUID *iid, PCWSTR source, UINT id, ...)
{
    BSTR format = WshLoadString(id), message = nullptr;
    if (format) {
        va_list arguments;
        va_start(arguments, id);
        PWSTR text = nullptr;
        if (FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_STRING, format, 0, 0, (LPWSTR)&text, 0, &arguments)) {
            message = SysAllocString(text);
            LocalFree(text);
        }
        va_end(arguments);
    }
    WshSignalText(iid, source, message);
    SysFreeString(format);
    SysFreeString(message);
    return DISP_E_EXCEPTION;
}

/* ------------------------------------------------------------------ the base of the objects */

typedef struct DISP_OBJECT DISP_OBJECT;
struct DISP_OBJECT {
    const void *Vtbl;
    IProvideClassInfo Info;
    LONG Refs;
    const GUID *Iid;
    const GUID *Class;
    const GUID *Accepted[3];
    void (*Destroy)(DISP_OBJECT *);
};

static DISP_OBJECT *object_of(void *This)
{
    return (DISP_OBJECT *)This;
}

static HRESULT STDMETHODCALLTYPE disp_query_interface(void *This, REFIID riid, void **out)
{
    if (!out)
        return E_POINTER;
    DISP_OBJECT *object = object_of(This);
    *out = nullptr;
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IDispatch) || IsEqualIID(riid, object->Iid)) {
        *out = object;
    } else if (IsEqualIID(riid, &IID_IProvideClassInfo)) {
        *out = &object->Info;
    } else {
        for (int i = 0; i < 3 && object->Accepted[i]; i++)
            if (IsEqualIID(riid, object->Accepted[i])) {
                *out = object;
                break;
            }
    }
    if (!*out)
        return E_NOINTERFACE;
    InterlockedIncrement(&object->Refs);
    return S_OK;
}

static ULONG STDMETHODCALLTYPE disp_add_ref(void *This)
{
    return (ULONG)InterlockedIncrement(&object_of(This)->Refs);
}

static ULONG STDMETHODCALLTYPE disp_release(void *This)
{
    DISP_OBJECT *object = object_of(This);
    LONG refs = InterlockedDecrement(&object->Refs);
    if (!refs) {
        if (object->Destroy)
            object->Destroy(object);
        HeapFree(GetProcessHeap(), 0, object);
    }
    return (ULONG)refs;
}

static HRESULT STDMETHODCALLTYPE disp_get_type_info_count(void *This, UINT *count)
{
    if (!count)
        return E_POINTER;
    *count = 1;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE disp_get_type_info(void *This, UINT index, LCID lcid, ITypeInfo **info)
{
    if (!info)
        return E_POINTER;
    *info = nullptr;
    if (index)
        return DISP_E_BADINDEX;
    return WshTypeInfoOf(object_of(This)->Iid, info);
}

static HRESULT STDMETHODCALLTYPE disp_get_ids_of_names(void *This, REFIID riid, LPOLESTR *names, UINT count, LCID lcid, DISPID *ids)
{
    if (!IsEqualIID(riid, &IID_NULL))
        return DISP_E_UNKNOWNINTERFACE;
    ITypeInfo *info;
    HRESULT hr = WshTypeInfoOf(object_of(This)->Iid, &info);
    if (FAILED(hr))
        return hr;
    hr = info->lpVtbl->GetIDsOfNames(info, names, count, ids);
    info->lpVtbl->Release(info);
    return hr;
}

static HRESULT STDMETHODCALLTYPE disp_invoke(void *This, DISPID id, REFIID riid, LCID lcid, WORD flags, DISPPARAMS *params, VARIANT *result,
                           EXCEPINFO *excep, UINT *arg_err)
{
    if (!IsEqualIID(riid, &IID_NULL))
        return DISP_E_UNKNOWNINTERFACE;
    ITypeInfo *info;
    HRESULT hr = WshTypeInfoOf(object_of(This)->Iid, &info);
    if (FAILED(hr))
        return hr;
    hr = info->lpVtbl->Invoke(info, This, id, flags, params, result, excep, arg_err);
    info->lpVtbl->Release(info);
    return hr;
}

static HRESULT STDMETHODCALLTYPE info_query_interface(IProvideClassInfo *This, REFIID riid, void **out)
{
    DISP_OBJECT *object = (DISP_OBJECT *)((BYTE *)This - offsetof(DISP_OBJECT, Info));
    return disp_query_interface(object, riid, out);
}

static ULONG STDMETHODCALLTYPE info_add_ref(IProvideClassInfo *This)
{
    return disp_add_ref((BYTE *)This - offsetof(DISP_OBJECT, Info));
}

static ULONG STDMETHODCALLTYPE info_release(IProvideClassInfo *This)
{
    return disp_release((BYTE *)This - offsetof(DISP_OBJECT, Info));
}

static HRESULT STDMETHODCALLTYPE info_get_class_info(IProvideClassInfo *This, ITypeInfo **info)
{
    DISP_OBJECT *object = (DISP_OBJECT *)((BYTE *)This - offsetof(DISP_OBJECT, Info));
    if (!info)
        return E_POINTER;
    return WshTypeInfoOf(object->Class, info);
}

static const IProvideClassInfoVtbl g_info_vtbl = {info_query_interface, info_add_ref, info_release, info_get_class_info};

static DISP_OBJECT *new_object(SIZE_T size, const void *vtbl, const GUID *iid, const GUID *class_id, void (*destroy)(DISP_OBJECT *))
{
    DISP_OBJECT *object = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, size);
    if (!object)
        return nullptr;
    object->Vtbl = vtbl;
    object->Info.lpVtbl = &g_info_vtbl;
    object->Refs = 1;
    object->Iid = iid;
    object->Class = class_id;
    object->Destroy = destroy;
    return object;
}

#define DISP_METHODS                                                                                                   \
    (void *)disp_query_interface, (void *)disp_add_ref, (void *)disp_release, (void *)disp_get_type_info_count,        \
        (void *)disp_get_type_info, (void *)disp_get_ids_of_names, (void *)disp_invoke

/* ------------------------------------------------------------------ enumerators */

typedef struct ENUM_VARIANT {
    IEnumVARIANT Enum;
    LONG Refs;
    VARIANT *Items;
    LONG Count, Position;
} ENUM_VARIANT;

static HRESULT STDMETHODCALLTYPE enum_query_interface(IEnumVARIANT *This, REFIID riid, void **out)
{
    if (!out)
        return E_POINTER;
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IEnumVARIANT)) {
        *out = This;
        InterlockedIncrement(&((ENUM_VARIANT *)This)->Refs);
        return S_OK;
    }
    *out = nullptr;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE enum_add_ref(IEnumVARIANT *This)
{
    return (ULONG)InterlockedIncrement(&((ENUM_VARIANT *)This)->Refs);
}

static ULONG STDMETHODCALLTYPE enum_release(IEnumVARIANT *This)
{
    ENUM_VARIANT *self = (ENUM_VARIANT *)This;
    LONG refs = InterlockedDecrement(&self->Refs);
    if (!refs) {
        for (LONG i = 0; i < self->Count; i++)
            VariantClear(&self->Items[i]);
        HeapFree(GetProcessHeap(), 0, self->Items);
        HeapFree(GetProcessHeap(), 0, self);
    }
    return (ULONG)refs;
}

static HRESULT STDMETHODCALLTYPE enum_next(IEnumVARIANT *This, ULONG count, VARIANT *out, ULONG *fetched)
{
    ENUM_VARIANT *self = (ENUM_VARIANT *)This;
    ULONG done = 0;
    while (done < count && self->Position < self->Count) {
        VariantInit(&out[done]);
        VariantCopy(&out[done], &self->Items[self->Position]);
        done++;
        self->Position++;
    }
    if (fetched)
        *fetched = done;
    return done == count ? S_OK : S_FALSE;
}

static HRESULT STDMETHODCALLTYPE enum_skip(IEnumVARIANT *This, ULONG count)
{
    ENUM_VARIANT *self = (ENUM_VARIANT *)This;
    self->Position += (LONG)count;
    if (self->Position > self->Count) {
        self->Position = self->Count;
        return S_FALSE;
    }
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE enum_reset(IEnumVARIANT *This)
{
    ((ENUM_VARIANT *)This)->Position = 0;
    return S_OK;
}

static HRESULT new_enum(const VARIANT *items, LONG count, LONG position, IEnumVARIANT **out);

static HRESULT STDMETHODCALLTYPE enum_clone(IEnumVARIANT *This, IEnumVARIANT **out)
{
    ENUM_VARIANT *self = (ENUM_VARIANT *)This;
    return new_enum(self->Items, self->Count, self->Position, out);
}

static const IEnumVARIANTVtbl g_enum_vtbl = {enum_query_interface, enum_add_ref, enum_release, enum_next, enum_skip, enum_reset, enum_clone};

static HRESULT new_enum(const VARIANT *items, LONG count, LONG position, IEnumVARIANT **out)
{
    ENUM_VARIANT *self = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*self));
    if (!self)
        return E_OUTOFMEMORY;
    self->Items = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, (count ? count : 1) * sizeof(VARIANT));
    if (!self->Items) {
        HeapFree(GetProcessHeap(), 0, self);
        return E_OUTOFMEMORY;
    }
    for (LONG i = 0; i < count; i++)
        VariantCopy(&self->Items[i], (VARIANT *)&items[i]);
    self->Enum.lpVtbl = &g_enum_vtbl;
    self->Refs = 1;
    self->Count = count;
    self->Position = position;
    *out = &self->Enum;
    return S_OK;
}

/* ------------------------------------------------------------------ the arguments */

typedef struct RUNTIME_NODE {
    struct RUNTIME_NODE *Next;
    BOOL Named;
    PWSTR Name, Help;
    int Type;
    int Count, Required;
    BOOL Optional;
} RUNTIME_NODE;

typedef struct ARGUMENTS {
    DISP_OBJECT Base;
    int Count;
    BSTR *All;
    int NamedCount, UnnamedCount;
    BSTR *Names;
    VARIANT *Values;
    BSTR *Unnamed;
    IWSHNamedArguments *Named;
    IWSHUnnamedArguments *UnnamedObject;
    RUNTIME_NODE *First, *Last;
    BSTR Usage, Description, Example;
    BOOL HasRuntime;
} ARGUMENTS;

typedef struct NAMED_ARGUMENTS {
    DISP_OBJECT Base;
    ARGUMENTS *Owner;
} NAMED_ARGUMENTS;

typedef struct UNNAMED_ARGUMENTS {
    DISP_OBJECT Base;
    ARGUMENTS *Owner;
} UNNAMED_ARGUMENTS;

static HRESULT STDMETHODCALLTYPE args_item(IArguments2 *This, LONG index, BSTR *value)
{
    ARGUMENTS *self = (ARGUMENTS *)This;
    if (!value)
        return E_POINTER;
    if (index < 0 || index >= self->Count)
        return DISP_E_BADINDEX;
    *value = SysAllocString(self->All[index]);
    return *value || !self->All[index] ? S_OK : E_OUTOFMEMORY;
}

static HRESULT STDMETHODCALLTYPE args_count(IArguments2 *This, LONG *count)
{
    if (!count)
        return E_POINTER;
    *count = ((ARGUMENTS *)This)->Count;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE args_new_enum(IArguments2 *This, IUnknown **out)
{
    ARGUMENTS *self = (ARGUMENTS *)This;
    if (!out)
        return E_POINTER;
    VARIANT *items = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, (self->Count ? self->Count : 1) * sizeof(VARIANT));
    if (!items)
        return E_OUTOFMEMORY;
    for (int i = 0; i < self->Count; i++) {
        V_VT(&items[i]) = VT_BSTR;
        V_BSTR(&items[i]) = self->All[i];
    }
    IEnumVARIANT *enumerator;
    HRESULT hr = new_enum(items, self->Count, 0, &enumerator);
    HeapFree(GetProcessHeap(), 0, items);
    *out = SUCCEEDED(hr) ? (IUnknown *)enumerator : nullptr;
    return hr;
}

static HRESULT STDMETHODCALLTYPE args_get_named(IArguments2 *This, IWSHNamedArguments **named)
{
    ARGUMENTS *self = (ARGUMENTS *)This;
    if (!named)
        return E_POINTER;
    *named = self->Named;
    self->Named->lpVtbl->AddRef(self->Named);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE args_get_unnamed(IArguments2 *This, IWSHUnnamedArguments **unnamed)
{
    ARGUMENTS *self = (ARGUMENTS *)This;
    if (!unnamed)
        return E_POINTER;
    *unnamed = self->UnnamedObject;
    self->UnnamedObject->lpVtbl->AddRef(self->UnnamedObject);
    return S_OK;
}

static HRESULT build_usage(ARGUMENTS *self, BSTR *out);

static HRESULT STDMETHODCALLTYPE args_show_usage(IArguments2 *This)
{
    ARGUMENTS *self = (ARGUMENTS *)This;
    BSTR text = nullptr;
    HRESULT hr;
    if (!self->HasRuntime) {
        hr = WshPrint(self->Description ? self->Description : L"");
        return FAILED(hr) ? hr : S_OK;
    }
    hr = build_usage(self, &text);
    if (SUCCEEDED(hr))
        hr = WshPrint(text);
    SysFreeString(text);
    return FAILED(hr) ? hr : S_OK;
}

static const void *g_arguments_vtbl[] = {
    DISP_METHODS, (void *)args_item, (void *)args_count, (void *)args_count, (void *)args_new_enum, (void *)args_get_named,
    (void *)args_get_unnamed, (void *)args_show_usage,
};

/* the named arguments */

static NAMED_ARGUMENTS *named_of(void *This)
{
    return (NAMED_ARGUMENTS *)This;
}

static int find_name(ARGUMENTS *owner, PCWSTR name)
{
    SIZE_T length = SysStringLen((BSTR)name);
    for (int i = 0; i < owner->NamedCount; i++)
        if (SysStringLen(owner->Names[i]) == length && (!length || !_wcsicmp(owner->Names[i], name)))
            return i;
    return -1;
}

static HRESULT STDMETHODCALLTYPE named_item(IWSHNamedArguments *This, BSTR name, VARIANT *value)
{
    ARGUMENTS *owner = named_of(This)->Owner;
    if (!value)
        return E_POINTER;
    VariantInit(value);
    int at = find_name(owner, name);
    return at < 0 ? S_OK : VariantCopy(value, &owner->Values[at]);
}

static HRESULT STDMETHODCALLTYPE named_count(IWSHNamedArguments *This, LONG *count)
{
    if (!count)
        return E_POINTER;
    *count = named_of(This)->Owner->NamedCount;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE named_exists(IWSHNamedArguments *This, BSTR name, VARIANT_BOOL *exists)
{
    if (!exists)
        return E_POINTER;
    *exists = find_name(named_of(This)->Owner, name) >= 0 ? VARIANT_TRUE : VARIANT_FALSE;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE named_new_enum(IWSHNamedArguments *This, IUnknown **out)
{
    ARGUMENTS *owner = named_of(This)->Owner;
    if (!out)
        return E_POINTER;
    VARIANT *items = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, (owner->NamedCount ? owner->NamedCount : 1) * sizeof(VARIANT));
    if (!items)
        return E_OUTOFMEMORY;
    for (int i = 0; i < owner->NamedCount; i++) {
        V_VT(&items[i]) = VT_BSTR;
        V_BSTR(&items[i]) = owner->Names[i];
    }
    IEnumVARIANT *enumerator;
    HRESULT hr = new_enum(items, owner->NamedCount, 0, &enumerator);
    HeapFree(GetProcessHeap(), 0, items);
    *out = SUCCEEDED(hr) ? (IUnknown *)enumerator : nullptr;
    return hr;
}

static const void *g_named_vtbl[] = {
    DISP_METHODS, (void *)named_item, (void *)named_count, (void *)named_count, (void *)named_exists, (void *)named_new_enum,
};

/* the unnamed arguments */

static HRESULT STDMETHODCALLTYPE unnamed_item(IWSHUnnamedArguments *This, LONG index, VARIANT *value)
{
    ARGUMENTS *owner = ((UNNAMED_ARGUMENTS *)This)->Owner;
    if (!value)
        return E_POINTER;
    VariantInit(value);
    if (index < 0 || index >= owner->UnnamedCount)
        return DISP_E_BADINDEX;
    V_VT(value) = VT_BSTR;
    V_BSTR(value) = SysAllocString(owner->Unnamed[index]);
    return V_BSTR(value) || !owner->Unnamed[index] ? S_OK : E_OUTOFMEMORY;
}

static HRESULT STDMETHODCALLTYPE unnamed_count(IWSHUnnamedArguments *This, LONG *count)
{
    if (!count)
        return E_POINTER;
    *count = ((UNNAMED_ARGUMENTS *)This)->Owner->UnnamedCount;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE unnamed_new_enum(IWSHUnnamedArguments *This, IUnknown **out)
{
    ARGUMENTS *owner = ((UNNAMED_ARGUMENTS *)This)->Owner;
    if (!out)
        return E_POINTER;
    VARIANT *items = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, (owner->UnnamedCount ? owner->UnnamedCount : 1) * sizeof(VARIANT));
    if (!items)
        return E_OUTOFMEMORY;
    for (int i = 0; i < owner->UnnamedCount; i++) {
        V_VT(&items[i]) = VT_BSTR;
        V_BSTR(&items[i]) = owner->Unnamed[i];
    }
    IEnumVARIANT *enumerator;
    HRESULT hr = new_enum(items, owner->UnnamedCount, 0, &enumerator);
    HeapFree(GetProcessHeap(), 0, items);
    *out = SUCCEEDED(hr) ? (IUnknown *)enumerator : nullptr;
    return hr;
}

static const void *g_unnamed_vtbl[] = {
    DISP_METHODS, (void *)unnamed_item, (void *)unnamed_count, (void *)unnamed_count, (void *)unnamed_new_enum,
};

static void free_node(RUNTIME_NODE *node)
{
    HeapFree(GetProcessHeap(), 0, node->Name);
    HeapFree(GetProcessHeap(), 0, node->Help);
    HeapFree(GetProcessHeap(), 0, node);
}

static void arguments_destroy(DISP_OBJECT *base)
{
    ARGUMENTS *self = (ARGUMENTS *)base;
    for (int i = 0; i < self->Count; i++)
        SysFreeString(self->All[i]);
    for (int i = 0; i < self->NamedCount; i++) {
        SysFreeString(self->Names[i]);
        VariantClear(&self->Values[i]);
    }
    for (int i = 0; i < self->UnnamedCount; i++)
        SysFreeString(self->Unnamed[i]);
    HeapFree(GetProcessHeap(), 0, self->All);
    HeapFree(GetProcessHeap(), 0, self->Names);
    HeapFree(GetProcessHeap(), 0, self->Values);
    HeapFree(GetProcessHeap(), 0, self->Unnamed);
    if (self->Named)
        self->Named->lpVtbl->Release(self->Named);
    if (self->UnnamedObject)
        self->UnnamedObject->lpVtbl->Release(self->UnnamedObject);
    while (self->First) {
        RUNTIME_NODE *next = self->First->Next;
        free_node(self->First);
        self->First = next;
    }
    SysFreeString(self->Usage);
    SysFreeString(self->Description);
    SysFreeString(self->Example);
}

static void sub_destroy(DISP_OBJECT *base)
{
    /* the owner is released with the collection. */
    ARGUMENTS *owner = ((NAMED_ARGUMENTS *)base)->Owner;
    ((IUnknown *)owner)->lpVtbl->Release((IUnknown *)owner);
}

/* the arguments that start with a slash are named: /name:value, /name+ and /name- and /name alone. The rest are unnamed. */
HRESULT WshCreateArguments(int count, PWSTR *arguments, IArguments2 **out)
{
    ARGUMENTS *self = (ARGUMENTS *)new_object(sizeof(ARGUMENTS), g_arguments_vtbl, &IID_IArguments2, &CLSID_IArguments_Class, arguments_destroy);
    if (!self)
        return E_OUTOFMEMORY;
    self->Base.Accepted[0] = &IID_IArguments;
    size_t slots = count > 0 ? (size_t)count : 1;
    self->All = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, slots * sizeof(BSTR));
    self->Names = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, slots * sizeof(BSTR));
    self->Values = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, slots * sizeof(VARIANT));
    self->Unnamed = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, slots * sizeof(BSTR));
    if (!self->All || !self->Names || !self->Values || !self->Unnamed) {
        disp_release(self);
        return E_OUTOFMEMORY;
    }
    for (int i = 0; i < count; i++) {
        PCWSTR word = arguments[i];
        self->All[self->Count++] = SysAllocString(word);
        if (word[0] != L'/') {
            self->Unnamed[self->UnnamedCount++] = SysAllocString(word);
            continue;
        }
        PCWSTR name = word + 1, at = name;
        int length = 0, kind = 0;
        while (*at) {
            if (*at == L':') {
                kind = 1;
                at++;
                break;
            }
            if (!at[1] && (*at == L'+' || *at == L'-')) {
                kind = 2;
                break;
            }
            length++;
            at++;
        }
        int slot = self->NamedCount++;
        self->Names[slot] = SysAllocStringLen(name, length);
        if (kind == 1) {
            V_VT(&self->Values[slot]) = VT_BSTR;
            V_BSTR(&self->Values[slot]) = SysAllocString(at);
        } else if (kind == 2) {
            V_VT(&self->Values[slot]) = VT_BOOL;
            V_BOOL(&self->Values[slot]) = *at == L'+' ? VARIANT_TRUE : VARIANT_FALSE;
        }
    }
    NAMED_ARGUMENTS *named = (NAMED_ARGUMENTS *)new_object(sizeof(NAMED_ARGUMENTS), g_named_vtbl, &IID_IWSHNamedArguments,
                                                           &CLSID_IWSHNamedArguments_Class, sub_destroy);
    UNNAMED_ARGUMENTS *unnamed = (UNNAMED_ARGUMENTS *)new_object(sizeof(UNNAMED_ARGUMENTS), g_unnamed_vtbl, &IID_IWSHUnnamedArguments,
                                                                 &CLSID_IWSHUnnamedArguments_Class, sub_destroy);
    if (!named || !unnamed) {
        if (named)
            HeapFree(GetProcessHeap(), 0, named);
        if (unnamed)
            HeapFree(GetProcessHeap(), 0, unnamed);
        disp_release(self);
        return E_OUTOFMEMORY;
    }
    named->Owner = unnamed->Owner = self;
    InterlockedIncrement(&self->Base.Refs);
    InterlockedIncrement(&self->Base.Refs);
    self->Named = (IWSHNamedArguments *)named;
    self->UnnamedObject = (IWSHUnnamedArguments *)unnamed;
    *out = (IArguments2 *)self;
    return S_OK;
}

HRESULT WshArgumentsAddRuntimeNamed(IArguments2 *arguments, PCWSTR name, PCWSTR help, int type, BOOL required)
{
    ARGUMENTS *self = (ARGUMENTS *)arguments;
    RUNTIME_NODE *node = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*node));
    if (!node)
        return E_OUTOFMEMORY;
    node->Named = TRUE;
    node->Name = duplicate(name);
    node->Help = duplicate(help);
    node->Type = type;
    node->Required = required;
    if (self->Last)
        self->Last->Next = node;
    else
        self->First = node;
    self->Last = node;
    self->HasRuntime = TRUE;
    return S_OK;
}

HRESULT WshArgumentsAddRuntimeUnnamed(IArguments2 *arguments, PCWSTR name, PCWSTR help, int count, int required)
{
    ARGUMENTS *self = (ARGUMENTS *)arguments;
    RUNTIME_NODE *node = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*node));
    if (!node)
        return E_OUTOFMEMORY;
    node->Name = duplicate(name);
    node->Help = duplicate(help);
    node->Count = count;
    node->Required = required;
    if (self->Last)
        self->Last->Next = node;
    else
        self->First = node;
    self->Last = node;
    self->HasRuntime = TRUE;
    return S_OK;
}

/* which: 0 the usage line, 1 the description, 2 the example. */
HRESULT WshArgumentsSetText(IArguments2 *arguments, int which, PCWSTR text)
{
    ARGUMENTS *self = (ARGUMENTS *)arguments;
    BSTR *slot = which == 0 ? &self->Usage : which == 1 ? &self->Description : &self->Example;
    SysFreeString(*slot);
    *slot = SysAllocString(text);
    return *slot ? S_OK : E_OUTOFMEMORY;
}

static HRESULT build_usage(ARGUMENTS *self, BSTR *out)
{
    *out = nullptr;
    return E_NOTIMPL;
}

/* ------------------------------------------------------------------ the host object, WScript */

typedef struct HOST_OBJECT {
    DISP_OBJECT Base;
    IArguments2 *Arguments;
    ITextStream *In, *Out, *Err;
} HOST_OBJECT;

static HRESULT bstr_out(BSTR *out, PCWSTR text)
{
    if (!out)
        return E_POINTER;
    *out = SysAllocString(text);
    return *out ? S_OK : E_OUTOFMEMORY;
}

static HRESULT STDMETHODCALLTYPE host_get_name(IHost *This, BSTR *value)
{
    if (!value)
        return E_POINTER;
    *value = WshLoadString(RES_BASE + 1);
    return *value ? S_OK : E_OUTOFMEMORY;
}

static HRESULT STDMETHODCALLTYPE host_get_application(IHost *This, IDispatch **value)
{
    if (!value)
        return E_POINTER;
    *value = (IDispatch *)This;
    disp_add_ref(This);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE host_get_full_name(IHost *This, BSTR *value)
{
    return bstr_out(value, g_host.Module);
}

static HRESULT STDMETHODCALLTYPE host_get_path(IHost *This, BSTR *value)
{
    return bstr_out(value, g_host.ModuleDirectory);
}

static HRESULT STDMETHODCALLTYPE host_get_interactive(IHost *This, VARIANT_BOOL *value)
{
    if (!value)
        return E_POINTER;
    *value = g_host.Batch ? VARIANT_FALSE : VARIANT_TRUE;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE host_put_interactive(IHost *This, VARIANT_BOOL value)
{
    g_host.Batch = value == VARIANT_FALSE;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE host_quit(IHost *This, int code)
{
    g_host.ExitCode = code;
    g_host.Quit = TRUE;
    return FAILED(WshInterrupt(WSH_E_QUIT)) ? WshInterrupt(WSH_E_QUIT) : S_OK;
}

static HRESULT STDMETHODCALLTYPE host_get_script_name(IHost *This, BSTR *value)
{
    PCWSTR path = g_host.ScriptPath, name = path;
    for (PCWSTR p = path; p && *p; p++)
        if (*p == L'\\' || *p == L'/')
            name = p + 1;
    return bstr_out(value, name);
}

static HRESULT STDMETHODCALLTYPE host_get_script_full_name(IHost *This, BSTR *value)
{
    return bstr_out(value, g_host.ScriptPath);
}

static HRESULT STDMETHODCALLTYPE host_get_arguments(IHost *This, IArguments2 **value)
{
    HOST_OBJECT *self = (HOST_OBJECT *)This;
    if (!value)
        return E_POINTER;
    *value = self->Arguments;
    self->Arguments->lpVtbl->AddRef(self->Arguments);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE host_get_version(IHost *This, BSTR *value)
{
    WCHAR text[32];
    _snwprintf(text, RTL_NUMBER_OF(text), L"%u.%u", (unsigned)g_host.VersionMajor, (unsigned)g_host.VersionMinor);
    return bstr_out(value, text);
}

static HRESULT STDMETHODCALLTYPE host_get_build_version(IHost *This, int *value)
{
    if (!value)
        return E_POINTER;
    *value = g_host.VersionRevision;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE host_get_timeout(IHost *This, LONG *value)
{
    if (!value)
        return E_POINTER;
    *value = (LONG)g_host.Timeout;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE host_put_timeout(IHost *This, LONG value)
{
    if (value < 0)
        return HRESULT_FROM_WIN32(ERROR_INVALID_PARAMETER);
    if (g_host.TimeoutSet)
        return S_FALSE;
    g_host.Timeout = (DWORD)value;
    return WshStartTimer((DWORD)value);
}

/* the class of a program id, made, and the error a script sees when there is none. */
static HRESULT make_object(PCWSTR progid, GUID *clsid, IUnknown **object)
{
    HRESULT hr = CLSIDFromProgID(progid, clsid);
    if (FAILED(hr)) {
        if (hr == (HRESULT)0x800401F3) {
            WshSignal(&IID_IHost, L"WScript.CreateObject", 3101, progid);
            return DISP_E_EXCEPTION;
        }
        BSTR text = nullptr;
        WshFormatHResult(hr, &text);
        WshSignalText(&IID_IHost, L"WScript.CreateObject", text);
        SysFreeString(text);
        return hr;
    }
    hr = CoCreateInstance(clsid, nullptr, CLSCTX_INPROC_SERVER | CLSCTX_LOCAL_SERVER | CLSCTX_REMOTE_SERVER, &IID_IUnknown, (void **)object);
    if (FAILED(hr))
        WshSignal(&IID_IHost, L"WScript.CreateObject", 3103, progid);
    return hr;
}

static HRESULT STDMETHODCALLTYPE host_create_object(IHost *This, BSTR progid, BSTR prefix, IDispatch **object)
{
    if (!object)
        return E_POINTER;
    *object = nullptr;
    if (!progid || !*progid)
        return E_INVALIDARG;
    if (prefix && !*prefix)
        prefix = nullptr;
    GUID clsid = {};
    IUnknown *unknown = nullptr;
    IDispatch *dispatch = nullptr;
    HRESULT hr = make_object(progid, &clsid, &unknown);
    if (SUCCEEDED(hr))
        hr = unknown->lpVtbl->QueryInterface(unknown, &IID_IDispatch, (void **)&dispatch);
    if (SUCCEEDED(hr) && prefix && FAILED(WshConnectObject(&clsid, dispatch, prefix))) {
        WshSignal(&IID_IHost, L"WScript.CreateObject", 3102);
        dispatch->lpVtbl->Release(dispatch);
        dispatch = nullptr;
        hr = DISP_E_EXCEPTION;
    }
    if (unknown)
        unknown->lpVtbl->Release(unknown);
    if (SUCCEEDED(hr))
        *object = dispatch;
    return hr;
}

static HRESULT STDMETHODCALLTYPE host_echo(IHost *This, SAFEARRAY *args)
{
    if (!args)
        return S_OK;
    LONG lower = 0, upper = -1;
    SafeArrayGetLBound(args, 1, &lower);
    SafeArrayGetUBound(args, 1, &upper);
    SIZE_T capacity = 1024, used = 0;
    PWSTR text = HeapAlloc(GetProcessHeap(), 0, capacity * sizeof(WCHAR));
    if (!text)
        return E_OUTOFMEMORY;
    text[0] = 0;
    HRESULT hr = S_OK;
    for (LONG i = lower; i <= upper && SUCCEEDED(hr); i++) {
        VARIANT element, converted;
        VariantInit(&element);
        VariantInit(&converted);
        hr = SafeArrayGetElement(args, &i, &element);
        if (SUCCEEDED(hr)) {
            if (V_VT(&element) == VT_NULL) {
                V_VT(&converted) = VT_BSTR;
                V_BSTR(&converted) = SysAllocString(L"null");
            } else
                hr = VariantChangeType(&converted, &element, 0, VT_BSTR);
        }
        if (SUCCEEDED(hr)) {
            SIZE_T length = V_BSTR(&converted) ? SysStringLen(V_BSTR(&converted)) : 0;
            if (used + length + 2 > capacity) {
                capacity = (used + length + 2) * 3 / 2;
                PWSTR grown = HeapReAlloc(GetProcessHeap(), 0, text, capacity * sizeof(WCHAR));
                if (!grown)
                    hr = E_OUTOFMEMORY;
                else
                    text = grown;
            }
            if (SUCCEEDED(hr)) {
                if (length)
                    memcpy(text + used, V_BSTR(&converted), length * sizeof(WCHAR));
                used += length;
                if (i != upper)
                    text[used++] = L' ';
                text[used] = 0;
            }
        }
        VariantClear(&element);
        VariantClear(&converted);
    }
    if (SUCCEEDED(hr))
        hr = WshPrint(text);
    HeapFree(GetProcessHeap(), 0, text);
    return FAILED(hr) ? hr : S_OK;
}

static HRESULT STDMETHODCALLTYPE host_get_object(IHost *This, BSTR path, BSTR progid, BSTR prefix, IDispatch **object)
{
    if (!object)
        return E_POINTER;
    *object = nullptr;
    if (path && !*path)
        path = nullptr;
    if (progid && !*progid)
        progid = nullptr;
    if (prefix && !*prefix)
        prefix = nullptr;
    GUID clsid = {};
    IDispatch *dispatch = nullptr;
    HRESULT hr;
    if (progid) {
        IUnknown *unknown = nullptr;
        hr = make_object(progid, &clsid, &unknown);
        if (FAILED(hr))
            return hr;
        if (path) {
            IPersistFile *file;
            hr = unknown->lpVtbl->QueryInterface(unknown, &IID_IPersistFile, (void **)&file);
            if (SUCCEEDED(hr)) {
                hr = file->lpVtbl->Load(file, path, STGM_READWRITE);
                file->lpVtbl->Release(file);
            }
        } else
            hr = S_OK;
        if (SUCCEEDED(hr))
            hr = unknown->lpVtbl->QueryInterface(unknown, &IID_IDispatch, (void **)&dispatch);
        unknown->lpVtbl->Release(unknown);
    } else {
        if (!path)
            return HRESULT_FROM_WIN32(ERROR_INVALID_PARAMETER);
        IBindCtx *context;
        IMoniker *moniker = nullptr;
        hr = CreateBindCtx(0, &context);
        if (FAILED(hr))
            return hr;
        WCHAR full[MAX_PATH];
        PCWSTR name = path;
        PCWSTR colon = wcschr(path, L':');
        if (!colon || colon - path == 1) {
            PWSTR part;
            if (!GetFullPathNameW(path, MAX_PATH, full, &part)) {
                context->lpVtbl->Release(context);
                return HRESULT_FROM_WIN32(GetLastError());
            }
            name = full;
        }
        ULONG eaten = 0;
        hr = MkParseDisplayName(context, name, &eaten, &moniker);
        if (SUCCEEDED(hr)) {
            hr = moniker->lpVtbl->BindToObject(moniker, context, nullptr, &IID_IDispatch, (void **)&dispatch);
            if (hr == (HRESULT)0x800C0005)
                hr = (HRESULT)0x800401EA;
            if (SUCCEEDED(hr))
                moniker->lpVtbl->GetClassID(moniker, &clsid);
            moniker->lpVtbl->Release(moniker);
        }
        context->lpVtbl->Release(context);
    }
    if (FAILED(hr))
        return hr;
    if (prefix && FAILED(WshConnectObject(&clsid, dispatch, prefix))) {
        dispatch->lpVtbl->Release(dispatch);
        WshSignal(&IID_IHost, L"WScript.CreateObject", 3102);
        return DISP_E_EXCEPTION;
    }
    *object = dispatch;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE host_disconnect_object(IHost *This, IDispatch *object)
{
    if (!object)
        return E_POINTER;
    return WshDisconnectObject(object);
}

static HRESULT STDMETHODCALLTYPE host_sleep(IHost *This, LONG time)
{
    if (time < 0)
        return HRESULT_FROM_WIN32(ERROR_INVALID_PARAMETER);
    DWORD start = GetTickCount();
    for (;;) {
        DWORD elapsed = GetTickCount() - start;
        if (elapsed >= (DWORD)time)
            break;
        DWORD wait = (DWORD)time - elapsed;
        if (wait > 1000)
            wait = 1000;
        MsgWaitForMultipleObjectsEx(0, nullptr, wait, 0x1DFF, MWMO_INPUTAVAILABLE);
        MSG message;
        if (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE))
            DispatchMessageW(&message);
        if (g_host.Pending < 0)
            break;
    }
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE host_connect_object(IHost *This, IDispatch *object, BSTR prefix)
{
    if (!object)
        return E_POINTER;
    GUID none = {};
    HRESULT hr = WshConnectObject(&none, object, prefix);
    if (FAILED(hr)) {
        WshSignal(&IID_IHost, L"WScript.CreateObject", 3102);
        return DISP_E_EXCEPTION;
    }
    return S_OK;
}

/* the standard streams are the text streams of the scripting runtime. */
static HRESULT standard_stream(HOST_OBJECT *self, ITextStream **slot, StandardStreamTypes type, ITextStream **out)
{
    if (!out)
        return E_POINTER;
    *out = nullptr;
    if (!*slot) {
        IFileSystem3 *fso;
        HRESULT hr = CoCreateInstance(&CLSID_FileSystemObject, nullptr, CLSCTX_INPROC_SERVER, &IID_IFileSystem3, (void **)&fso);
        if (FAILED(hr))
            return hr;
        BOOL unicode = g_host.Unicode;
        if (type != StdIn && !unicode) {
            DWORD mode;
            unicode = GetConsoleMode(GetStdHandle(type == StdOut ? STD_OUTPUT_HANDLE : STD_ERROR_HANDLE), &mode);
        }
        hr = fso->lpVtbl->GetStandardStream(fso, type, unicode ? VARIANT_TRUE : VARIANT_FALSE, slot);
        fso->lpVtbl->Release(fso);
        if (FAILED(hr))
            return hr;
    }
    *out = *slot;
    (*slot)->lpVtbl->AddRef(*slot);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE host_get_std_in(IHost *This, ITextStream **stream)
{
    return standard_stream((HOST_OBJECT *)This, &((HOST_OBJECT *)This)->In, StdIn, stream);
}

static HRESULT STDMETHODCALLTYPE host_get_std_out(IHost *This, ITextStream **stream)
{
    return standard_stream((HOST_OBJECT *)This, &((HOST_OBJECT *)This)->Out, StdOut, stream);
}

static HRESULT STDMETHODCALLTYPE host_get_std_err(IHost *This, ITextStream **stream)
{
    return standard_stream((HOST_OBJECT *)This, &((HOST_OBJECT *)This)->Err, StdErr, stream);
}

static const void *g_host_vtbl[] = {
    DISP_METHODS, (void *)host_get_name, (void *)host_get_application, (void *)host_get_full_name, (void *)host_get_path,
    (void *)host_get_interactive, (void *)host_put_interactive, (void *)host_quit, (void *)host_get_script_name,
    (void *)host_get_script_full_name, (void *)host_get_arguments, (void *)host_get_version, (void *)host_get_build_version,
    (void *)host_get_timeout, (void *)host_put_timeout, (void *)host_create_object, (void *)host_echo, (void *)host_get_object,
    (void *)host_disconnect_object, (void *)host_sleep, (void *)host_connect_object, (void *)host_get_std_in,
    (void *)host_get_std_out, (void *)host_get_std_err,
};

static void host_destroy(DISP_OBJECT *base)
{
    HOST_OBJECT *self = (HOST_OBJECT *)base;
    if (self->Arguments)
        self->Arguments->lpVtbl->Release(self->Arguments);
    if (self->In)
        self->In->lpVtbl->Release(self->In);
    if (self->Out)
        self->Out->lpVtbl->Release(self->Out);
    if (self->Err)
        self->Err->lpVtbl->Release(self->Err);
}

HRESULT WshCreateHostObject(int count, PWSTR *arguments, HOST_OBJECT **object)
{
    HOST_OBJECT *self = (HOST_OBJECT *)new_object(sizeof(HOST_OBJECT), g_host_vtbl, &IID_IHost, &CLSID_IHost_Class, host_destroy);
    if (!self)
        return E_OUTOFMEMORY;
    HRESULT hr = WshCreateArguments(count, arguments, &self->Arguments);
    if (FAILED(hr)) {
        HeapFree(GetProcessHeap(), 0, self);
        return hr;
    }
    *object = self;
    return S_OK;
}

IArguments2 *WshHostArguments(HOST_OBJECT *object)
{
    return object->Arguments;
}

IDispatch *WshHostDispatch(HOST_OBJECT *object)
{
    return (IDispatch *)object;
}

void WshFreeHostObject(HOST_OBJECT *object)
{
    disp_release(object);
}
