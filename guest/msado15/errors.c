/*
 * the errors and messages of a connection. The list belongs to the connection, and the collection and the error objects
 * that were taken from it keep their own copies of what they show, so they stay valid when the connection goes.
 */
#include "adoprivate.h"

BSTR AdoSysString(LPCWSTR text)
{
    return SysAllocString(text ? text : L"");
}

ERROR_LIST *ErrorListCreate(void)
{
    ERROR_LIST *list = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*list));
    if (list)
        list->Refs = 1;
    return list;
}

void ErrorListAddRef(ERROR_LIST *list)
{
    InterlockedIncrement(&list->Refs);
}

static void error_data_free(ERROR_DATA *data)
{
    SysFreeString(data->Source);
    SysFreeString(data->Description);
    SysFreeString(data->State);
}

void ErrorListClear(ERROR_LIST *list)
{
    for (ULONG i = 0; i < list->Count; i++)
        error_data_free(&list->Items[i]);
    HeapFree(GetProcessHeap(), 0, list->Items);
    list->Items = nullptr;
    list->Count = 0;
}

void ErrorListRelease(ERROR_LIST *list)
{
    if (list && !InterlockedDecrement(&list->Refs)) {
        ErrorListClear(list);
        HeapFree(GetProcessHeap(), 0, list);
    }
}

HRESULT ErrorListAdd(ERROR_LIST *list, LONG number, LONG native, LPCWSTR source, LPCWSTR description, LPCWSTR state)
{
    ERROR_DATA *items = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, (list->Count + 1) * sizeof(*items));
    if (!items)
        return E_OUTOFMEMORY;
    if (list->Count)
        memcpy(items, list->Items, list->Count * sizeof(*items));
    ERROR_DATA *added = &items[list->Count];
    added->Number = number;
    added->Native = native;
    added->Source = AdoSysString(source);
    added->Description = AdoSysString(description);
    added->State = AdoSysString(state);
    HeapFree(GetProcessHeap(), 0, list->Items);
    list->Items = items;
    list->Count++;
    return S_OK;
}

/* the text of a message of a driver without the names in brackets that a driver puts before it. */
static LPCWSTR message_text(LPCWSTR text)
{
    while (*text == L'[') {
        LPCWSTR close = wcschr(text, L']');
        if (!close)
            break;
        text = close + 1;
    }
    return text;
}

void ErrorListAddDiagnostics(ERROR_LIST *list, SQLSMALLINT type, SQLHANDLE handle, LONG error_number)
{
    for (SQLSMALLINT record = 1;; record++) {
        WCHAR state[8] = L"", message[1024] = L"";
        SQLINTEGER native = 0;
        SQLSMALLINT length;
        if (SQLGetDiagRecW(type, handle, record, state, &native, message, RTL_NUMBER_OF(message), &length) != SQL_SUCCESS)
            break;
        /* a state of the class 01 is a message, which has no number of its own. */
        BOOL info = state[0] == L'0' && state[1] == L'1';
        LONG number = info ? 0 : error_number;
        if (!info && !wcscmp(state, L"28000"))
            number = DB_SEC_E_AUTH_FAILED;
        ErrorListAdd(list, number, native, L"Microsoft OLE DB Provider for SQL Server", message_text(message), state);
    }
}

/* ------------------------------------------------------------------ an error */

typedef struct ERROR_OBJECT {
    ADOError Error;
    LONG Refs;
    ERROR_DATA Data;
} ERROR_OBJECT;

DISPATCH_STUBS(error, ADOError)

static HRESULT STDMETHODCALLTYPE error_query_interface(ADOError *This, REFIID riid, void **object)
{
    if (!object)
        return E_POINTER;
    *object = nullptr;
    if (!IsEqualIID(riid, &IID_IUnknown) && !IsEqualIID(riid, &IID_IDispatch) && !IsEqualIID(riid, &IID_ADOError))
        return E_NOINTERFACE;
    *object = This;
    This->lpVtbl->AddRef(This);
    return S_OK;
}

static ULONG STDMETHODCALLTYPE error_add_ref(ADOError *This)
{
    return InterlockedIncrement(&((ERROR_OBJECT *)This)->Refs);
}

static ULONG STDMETHODCALLTYPE error_release(ADOError *This)
{
    ERROR_OBJECT *object = (ERROR_OBJECT *)This;
    LONG refs = InterlockedDecrement(&object->Refs);
    if (!refs) {
        error_data_free(&object->Data);
        HeapFree(GetProcessHeap(), 0, object);
        InterlockedDecrement(&g_ado_objects);
    }
    return refs;
}

static HRESULT get_long(LONG *out, LONG value)
{
    if (!out)
        return E_POINTER;
    *out = value;
    return S_OK;
}

static HRESULT get_text(BSTR *out, BSTR value)
{
    if (!out)
        return E_POINTER;
    *out = SysAllocStringLen(value, SysStringLen(value));
    return *out ? S_OK : E_OUTOFMEMORY;
}

static HRESULT STDMETHODCALLTYPE error_get_number(ADOError *This, LONG *value)
{
    return get_long(value, ((ERROR_OBJECT *)This)->Data.Number);
}

static HRESULT STDMETHODCALLTYPE error_get_source(ADOError *This, BSTR *value)
{
    return get_text(value, ((ERROR_OBJECT *)This)->Data.Source);
}

static HRESULT STDMETHODCALLTYPE error_get_description(ADOError *This, BSTR *value)
{
    return get_text(value, ((ERROR_OBJECT *)This)->Data.Description);
}

static HRESULT STDMETHODCALLTYPE error_get_help_file(ADOError *This, BSTR *value)
{
    if (!value)
        return E_POINTER;
    *value = AdoSysString(L"");
    return *value ? S_OK : E_OUTOFMEMORY;
}

static HRESULT STDMETHODCALLTYPE error_get_help_context(ADOError *This, LONG *value)
{
    return get_long(value, 0);
}

static HRESULT STDMETHODCALLTYPE error_get_sql_state(ADOError *This, BSTR *value)
{
    return get_text(value, ((ERROR_OBJECT *)This)->Data.State);
}

static HRESULT STDMETHODCALLTYPE error_get_native_error(ADOError *This, LONG *value)
{
    return get_long(value, ((ERROR_OBJECT *)This)->Data.Native);
}

static const ADOErrorVtbl g_error_vtbl = {
    error_query_interface, error_add_ref, error_release, error_GetTypeInfoCount, error_GetTypeInfo,
    error_GetIDsOfNames, error_Invoke, error_get_number, error_get_source, error_get_description,
    error_get_help_file, error_get_help_context, error_get_sql_state, error_get_native_error,
};

HRESULT ErrorCreate(const ERROR_DATA *data, ADOError **error)
{
    ERROR_OBJECT *object = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*object));
    if (!object)
        return E_OUTOFMEMORY;
    object->Error.lpVtbl = &g_error_vtbl;
    object->Refs = 1;
    object->Data.Number = data->Number;
    object->Data.Native = data->Native;
    object->Data.Source = SysAllocStringLen(data->Source, SysStringLen(data->Source));
    object->Data.Description = SysAllocStringLen(data->Description, SysStringLen(data->Description));
    object->Data.State = SysAllocStringLen(data->State, SysStringLen(data->State));
    InterlockedIncrement(&g_ado_objects);
    *error = &object->Error;
    return S_OK;
}

/* ------------------------------------------------------------------ the collection */

typedef struct ERRORS_OBJECT {
    ADOErrors Errors;
    LONG Refs;
    ERROR_LIST *List;
} ERRORS_OBJECT;

DISPATCH_STUBS(errors, ADOErrors)

static HRESULT STDMETHODCALLTYPE errors_query_interface(ADOErrors *This, REFIID riid, void **object)
{
    if (!object)
        return E_POINTER;
    *object = nullptr;
    if (!IsEqualIID(riid, &IID_IUnknown) && !IsEqualIID(riid, &IID_IDispatch) && !IsEqualIID(riid, &IID_ADOErrors))
        return E_NOINTERFACE;
    *object = This;
    This->lpVtbl->AddRef(This);
    return S_OK;
}

static ULONG STDMETHODCALLTYPE errors_add_ref(ADOErrors *This)
{
    return InterlockedIncrement(&((ERRORS_OBJECT *)This)->Refs);
}

static ULONG STDMETHODCALLTYPE errors_release(ADOErrors *This)
{
    ERRORS_OBJECT *object = (ERRORS_OBJECT *)This;
    LONG refs = InterlockedDecrement(&object->Refs);
    if (!refs) {
        ErrorListRelease(object->List);
        HeapFree(GetProcessHeap(), 0, object);
        InterlockedDecrement(&g_ado_objects);
    }
    return refs;
}

static HRESULT STDMETHODCALLTYPE errors_get_count(ADOErrors *This, LONG *value)
{
    return get_long(value, (LONG)((ERRORS_OBJECT *)This)->List->Count);
}

static HRESULT STDMETHODCALLTYPE errors_new_enum(ADOErrors *This, IUnknown **value)
{
    if (value)
        *value = nullptr;
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE errors_refresh(ADOErrors *This)
{
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE errors_get_item(ADOErrors *This, VARIANT index, ADOError **value)
{
    ERROR_LIST *list = ((ERRORS_OBJECT *)This)->List;
    if (!value)
        return E_POINTER;
    *value = nullptr;
    VARIANT number;
    VariantInit(&number);
    if (FAILED(VariantChangeType(&number, &index, 0, VT_I4)))
        return ADO_E_ITEM_NOT_FOUND;
    HRESULT result = number.lVal >= 0 && (ULONG)number.lVal < list->Count ? ErrorCreate(&list->Items[number.lVal], value)
                                                                         : ADO_E_ITEM_NOT_FOUND;
    return result;
}

static HRESULT STDMETHODCALLTYPE errors_clear(ADOErrors *This)
{
    ErrorListClear(((ERRORS_OBJECT *)This)->List);
    return S_OK;
}

static const ADOErrorsVtbl g_errors_vtbl = {
    errors_query_interface, errors_add_ref, errors_release, errors_GetTypeInfoCount, errors_GetTypeInfo,
    errors_GetIDsOfNames, errors_Invoke, errors_get_count, errors_new_enum, errors_refresh, errors_get_item, errors_clear,
};

HRESULT ErrorsCreate(ERROR_LIST *list, ADOErrors **errors)
{
    ERRORS_OBJECT *object = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*object));
    if (!object)
        return E_OUTOFMEMORY;
    object->Errors.lpVtbl = &g_errors_vtbl;
    object->Refs = 1;
    object->List = list;
    ErrorListAddRef(list);
    InterlockedIncrement(&g_ado_objects);
    *errors = &object->Errors;
    return S_OK;
}
