/*
 * Scripting.Dictionary: keys of any plain type with items of any type, kept in the order they were added, found through
 * 1201 chains by a hash of the key as Windows hashes it.
 */
#include "scrrun_private.h"

#define OBJECT_OF(type, member, pointer) ((type *)((char *)(pointer) - offsetof(type, member)))
#define BUCKETS 1201

typedef struct DICT_NODE {
    struct DICT_NODE *Order;
    VARIANT Key;
    VARIANT Item;
    struct DICT_NODE *Chain;
} DICT_NODE;

typedef struct DICT_ENUM DICT_ENUM;

typedef struct DICTIONARY {
    IDictionary Dictionary;
    IProvideClassInfo Info;
    IExternalConnection Connection;
    LONG Refs;
    LONG Connections;
    LONG Count;
    DICT_NODE *First, *Last;
    DICT_NODE **Table;
    DICT_ENUM *Enums;
    CompareMethod Mode;
    LCID Lcid;
    /* the value given to put_CompareMode, which get_CompareMode gives back. */
    LONG Requested;
} DICTIONARY;

struct DICT_ENUM {
    IEnumVARIANT Enum;
    LONG Refs;
    DICTIONARY *Dictionary;
    DICT_NODE *Current;
    DICT_ENUM *Next;
};

static const IDictionaryVtbl g_dictionary_vtbl;
static const IProvideClassInfoVtbl g_info_vtbl;
static const IExternalConnectionVtbl g_connection_vtbl;
static const IEnumVARIANTVtbl g_enum_vtbl;

MIME_DEFINE_GUID(CLSID_ScrDictionaryClass, 0xEE09B103, 0x97E0, 0x11CF, 0x97, 0x8F, 0x00, 0xA0, 0x24, 0x63, 0xE0, 0x6F);

static DICTIONARY *dictionary_of(IDictionary *This)
{
    return OBJECT_OF(DICTIONARY, Dictionary, This);
}

/* ------------------------------------------------------------------ keys */

/* a variant passed by reference seen as the value it points to, copied where it has to be. */
static VARIANT *strip_by_ref(VARIANT *copy, VARIANT *value)
{
    value = ScrDeref(value);
    if (!(value->vt & VT_BYREF))
        return value;
    VARTYPE type = value->vt & ~VT_BYREF;
    switch (type) {
    case VT_EMPTY:
    case VT_NULL:
        break;
    case VT_I2:
    case VT_BOOL:
        copy->iVal = *(SHORT *)value->byref;
        break;
    case VT_I4:
    case VT_R4:
    case VT_ERROR:
        copy->lVal = *(LONG *)value->byref;
        break;
    case VT_R8:
    case VT_CY:
    case VT_DATE:
    case VT_BSTR:
    case VT_DISPATCH:
    case VT_UNKNOWN:
        copy->llVal = *(LONGLONG *)value->byref;
        break;
    case VT_DECIMAL:
        *(DECIMAL *)copy = *(DECIMAL *)value->byref;
        break;
    case VT_UI1:
        copy->bVal = *(BYTE *)value->byref;
        break;
    default:
        if (!(type & VT_ARRAY))
            return value;
        copy->llVal = *(LONGLONG *)value->byref;
        break;
    }
    if (type != VT_DECIMAL)
        copy->vt = type;
    return copy;
}

/* the float a single of the key stands for, rounded through currency to four digits. */
static BOOL single_key(float value, float *out)
{
    float key = 0;
    if (value <= 9999999.0f && value >= -9999999.0f) {
        CY cy;
        if (FAILED(VarCyFromR4(value, &cy)) || FAILED(VarR4FromCy(cy, &key)))
            return FALSE;
    }
    *out = key;
    return TRUE;
}

static ULONG float_bits(float value)
{
    ULONG bits;
    memcpy(&bits, &value, sizeof(bits));
    return bits;
}

/* the hash of a key as a bucket number, FALSE when the key is of a type a dictionary cannot hold. */
static BOOL key_hash(DICTIONARY *dict, const VARIANT *key, ULONG *hash)
{
    ULONG bits = 0;
    *hash = (ULONG)-1;
    switch (key->vt) {
    case VT_BSTR: {
        BSTR text = key->bstrVal;
        BSTR lowered = nullptr;
        UINT length = SysStringLen(text);
        if (dict->Mode != BinaryCompare && length) {
            lowered = SysAllocStringLen(nullptr, length);
            if (lowered && !LCMapStringW(dict->Lcid, LCMAP_LOWERCASE, text, (int)length, lowered, (int)length)) {
                SysFreeString(lowered);
                lowered = nullptr;
            }
            if (lowered)
                text = lowered;
        }
        ULONG h = 0;
        for (UINT i = 0; i < length; i++)
            h = 17 * h + text[i];
        bits = h;
        SysFreeString(lowered);
        break;
    }
    case VT_EMPTY:
    case VT_NULL:
        break;
    case VT_I2:
    case VT_BOOL:
        bits = float_bits((float)key->iVal);
        break;
    case VT_UI1:
        bits = float_bits((float)key->bVal);
        break;
    case VT_I4:
        if ((ULONG)(key->lVal + 9999999) <= 0x1312CFE)
            bits = float_bits((float)key->lVal);
        break;
    case VT_ERROR:
        bits = float_bits((float)key->lVal);
        break;
    case VT_R4: {
        float value;
        if (!single_key(key->fltVal, &value))
            return FALSE;
        bits = float_bits(value);
        break;
    }
    case VT_R8:
    case VT_DATE: {
        double value = key->dblVal;
        if (value > 9999999.0 || value < -9999999.0)
            break;
        float single;
        if (key->vt == VT_R8) {
            float rounded = 0;
            CY cy;
            if (FAILED(VarCyFromR4((float)value, &cy)) || FAILED(VarR4FromCy(cy, &rounded)))
                return FALSE;
            single = rounded;
        } else if (!single_key((float)value, &single)) {
            return FALSE;
        }
        bits = float_bits(single);
        break;
    }
    case VT_CY: {
        CY high, low;
        VarCyFromI4(9999999, &high);
        VarCyFromI4(-9999999, &low);
        if (key->cyVal.int64 > high.int64 || key->cyVal.int64 < low.int64)
            break;
        float value;
        if (FAILED(VarR4FromCy(key->cyVal, &value)))
            return FALSE;
        bits = float_bits(value);
        break;
    }
    case VT_DISPATCH:
    case VT_UNKNOWN: {
        if (!key->punkVal)
            break;
        IUnknown *identity = nullptr;
        key->punkVal->lpVtbl->QueryInterface(key->punkVal, &IID_IUnknown, (void **)&identity);
        if (!identity)
            return FALSE;
        ULONGLONG address = (ULONGLONG)(ULONG_PTR)identity;
        bits = (ULONG)(address >> 32) ^ (ULONG)address;
        identity->lpVtbl->Release(identity);
        break;
    }
    case VT_DECIMAL: {
        DECIMAL high, low;
        VarDecFromI4(9999999, &high);
        VarDecFromI4(-9999999, &low);
        if (VarDecCmp(&key->decVal, &high) == VARCMP_GT || VarDecCmp(&key->decVal, &low) == VARCMP_LT)
            break;
        float value;
        if (FAILED(VarR4FromDec(&key->decVal, &value)))
            return FALSE;
        bits = float_bits(value);
        break;
    }
    default:
        return FALSE;
    }
    *hash = bits % BUCKETS;
    return TRUE;
}

static BOOL objects_equal(IUnknown *a, IUnknown *b)
{
    if (!a || !b)
        return a == b;
    IUnknown *ia = nullptr, *ib = nullptr;
    a->lpVtbl->QueryInterface(a, &IID_IUnknown, (void **)&ia);
    if (!ia)
        return FALSE;
    b->lpVtbl->QueryInterface(b, &IID_IUnknown, (void **)&ib);
    BOOL equal = ib && ia == ib;
    ia->lpVtbl->Release(ia);
    if (ib)
        ib->lpVtbl->Release(ib);
    return equal;
}

/* whether two keys are the same key. */
static BOOL keys_equal(DICTIONARY *dict, const VARIANT *a, const VARIANT *b)
{
    VARTYPE va = a->vt, vb = b->vt;
    if (va > VT_UI1 || vb > VT_UI1)
        return FALSE;
    if (va == VT_UNKNOWN)
        va = VT_DISPATCH;
    if (vb == VT_UNKNOWN)
        vb = VT_DISPATCH;
    if (va != vb) {
        if (va == VT_ERROR || vb == VT_ERROR || va == VT_DISPATCH || vb == VT_DISPATCH)
            return FALSE;
        return VarCmp((VARIANT *)a, (VARIANT *)b, dict->Lcid, 0) == VARCMP_EQ;
    }
    if (va == VT_DISPATCH)
        return objects_equal(a->punkVal, b->punkVal);
    if (va == VT_NULL)
        return TRUE;
    if (va == VT_BSTR) {
        UINT la = SysStringLen(a->bstrVal), lb = SysStringLen(b->bstrVal);
        if (dict->Mode == BinaryCompare)
            return la == lb && (!la || !memcmp(a->bstrVal, b->bstrVal, la * sizeof(WCHAR)));
        if (!la || !lb)
            return la == lb;
        return CompareStringW(dict->Lcid, NORM_IGNORECASE, a->bstrVal, (int)la, b->bstrVal, (int)lb) == CSTR_EQUAL;
    }
    return VarCmp((VARIANT *)a, (VARIANT *)b, dict->Lcid, 0) == VARCMP_EQ;
}

static DICT_NODE *find_key(DICTIONARY *dict, const VARIANT *key, ULONG hash)
{
    for (DICT_NODE *node = dict->Table[hash]; node; node = node->Chain) {
        if (keys_equal(dict, key, &node->Key))
            return node;
    }
    return nullptr;
}

static HRESULT add_node(DICTIONARY *dict, ULONG hash, const VARIANT *key, const VARIANT *item)
{
    DICT_NODE *node = ScrAlloc(sizeof(*node));
    if (!node)
        return E_OUTOFMEMORY;
    HRESULT hr = VariantCopy(&node->Key, (VARIANT *)key);
    if (SUCCEEDED(hr))
        hr = VariantCopy(&node->Item, (VARIANT *)item);
    if (FAILED(hr)) {
        VariantClear(&node->Key);
        VariantClear(&node->Item);
        ScrFree(node);
        return hr;
    }
    node->Chain = dict->Table[hash];
    dict->Table[hash] = node;
    if (dict->Last)
        dict->Last->Order = node;
    else
        dict->First = node;
    dict->Last = node;
    dict->Count++;
    return S_OK;
}

static void free_node(DICT_NODE *node)
{
    VariantClear(&node->Key);
    VariantClear(&node->Item);
    ScrFree(node);
}

/* ------------------------------------------------------------------ the object */

static HRESULT STDMETHODCALLTYPE dict_query_interface(IDictionary *This, REFIID riid, void **object)
{
    if (!object)
        return E_POINTER;
    DICTIONARY *dict = dictionary_of(This);
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IDispatch) || IsEqualIID(riid, &IID_IDictionary))
        *object = &dict->Dictionary;
    else if (IsEqualIID(riid, &IID_IProvideClassInfo))
        *object = &dict->Info;
    else if (IsEqualIID(riid, &IID_IExternalConnection))
        *object = &dict->Connection;
    else {
        *object = nullptr;
        return E_NOINTERFACE;
    }
    InterlockedIncrement(&dict->Refs);
    return S_OK;
}

static ULONG STDMETHODCALLTYPE dict_add_ref(IDictionary *This)
{
    return (ULONG)InterlockedIncrement(&dictionary_of(This)->Refs);
}

static void dict_remove_all_nodes(DICTIONARY *dict)
{
    DICT_NODE *node = dict->First;
    while (node) {
        DICT_NODE *next = node->Order;
        free_node(node);
        node = next;
    }
    dict->First = dict->Last = nullptr;
    dict->Count = 0;
    if (dict->Table)
        memset(dict->Table, 0, BUCKETS * sizeof(*dict->Table));
}

static ULONG STDMETHODCALLTYPE dict_release(IDictionary *This)
{
    DICTIONARY *dict = dictionary_of(This);
    LONG refs = InterlockedDecrement(&dict->Refs);
    if (!refs) {
        dict_remove_all_nodes(dict);
        for (DICT_ENUM *e = dict->Enums; e; e = e->Next)
            e->Dictionary = nullptr;
        ScrFree(dict->Table);
        ScrFree(dict);
        ScrUnlockDll();
    }
    return (ULONG)refs;
}

/* the type information of the dictionary for a language, falling back to the one of English. */
static HRESULT STDMETHODCALLTYPE dict_GetTypeInfoCount(IDictionary *This, UINT *count)
{
    if (!count)
        return E_POINTER;
    *count = 1;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE dict_GetTypeInfo(IDictionary *This, UINT index, LCID lcid, ITypeInfo **info)
{
    if (!info)
        return E_POINTER;
    *info = nullptr;
    if (index)
        return DISP_E_BADINDEX;
    return ScrTypeInfo(&IID_IDictionary, info);
}

static HRESULT STDMETHODCALLTYPE dict_GetIDsOfNames(IDictionary *This, REFIID riid, LPOLESTR *names, UINT count, LCID lcid,
                                                    DISPID *ids)
{
    if (!IsEqualIID(riid, &IID_NULL))
        return DISP_E_UNKNOWNINTERFACE;
    return ScrDispatchGetIDs(&IID_IDictionary, names, count, ids);
}

static HRESULT STDMETHODCALLTYPE dict_Invoke(IDictionary *This, DISPID id, REFIID riid, LCID lcid, WORD flags,
                                             DISPPARAMS *params, VARIANT *result, EXCEPINFO *excep, UINT *arg_error)
{
    return ScrDispatchInvoke(This, &IID_IDictionary, id, flags, params, result, excep, arg_error);
}

/* the item stored for a key: a new key gets an empty item or the value given. */
static HRESULT set_item(DICTIONARY *dict, VARIANT *key_in, VARIANT *item_in, BOOL by_reference)
{
    VARIANT key_copy, item_copy, default_value;
    VariantInit(&key_copy);
    VariantInit(&item_copy);
    VariantInit(&default_value);
    VARIANT *key = strip_by_ref(&key_copy, key_in);
    VARIANT *item = strip_by_ref(&item_copy, item_in);
    HRESULT hr = S_OK;
    if (!by_reference && item->vt == VT_DISPATCH) {
        IDispatch *object = item->pdispVal;
        DISPPARAMS none = {0};
        hr = object ? object->lpVtbl->Invoke(object, 0, &IID_NULL, 1033, DISPATCH_METHOD | DISPATCH_PROPERTYGET, &none,
                                             &default_value, nullptr, nullptr)
                    : E_VB_NOOBJECT;
        if (FAILED(hr))
            return hr;
        item = &default_value;
    }
    ULONG hash;
    if (!key_hash(dict, key, &hash)) {
        hr = E_VB_INVALID;
    } else {
        DICT_NODE *node = find_key(dict, key, hash);
        if (node) {
            VARIANT old = node->Item;
            VariantInit(&node->Item);
            hr = VariantCopy(&node->Item, item);
            VariantClear(&old);
        } else {
            hr = add_node(dict, hash, key, item);
        }
    }
    VariantClear(&default_value);
    return SUCCEEDED(hr) ? S_OK : hr;
}

static HRESULT STDMETHODCALLTYPE dict_putref_Item(IDictionary *This, VARIANT *key, VARIANT *item)
{
    return set_item(dictionary_of(This), key, item, TRUE);
}

static HRESULT STDMETHODCALLTYPE dict_put_Item(IDictionary *This, VARIANT *key, VARIANT *item)
{
    return set_item(dictionary_of(This), key, item, FALSE);
}

static HRESULT STDMETHODCALLTYPE dict_get_Item(IDictionary *This, VARIANT *key_in, VARIANT *item)
{
    DICTIONARY *dict = dictionary_of(This);
    VARIANT key_copy, empty;
    VariantInit(&key_copy);
    VariantInit(&empty);
    VARIANT *key = strip_by_ref(&key_copy, key_in);
    ULONG hash;
    if (!key_hash(dict, key, &hash))
        return E_VB_INVALID;
    DICT_NODE *node = find_key(dict, key, hash);
    if (node)
        return VariantCopy(item, &node->Item);
    item->vt = VT_EMPTY;
    return add_node(dict, hash, key, &empty);
}

static HRESULT STDMETHODCALLTYPE dict_Add(IDictionary *This, VARIANT *key_in, VARIANT *item_in)
{
    DICTIONARY *dict = dictionary_of(This);
    VARIANT key_copy, item_copy;
    VariantInit(&key_copy);
    VariantInit(&item_copy);
    VARIANT *key = strip_by_ref(&key_copy, key_in);
    VARIANT *item = strip_by_ref(&item_copy, item_in);
    ULONG hash;
    if (!key_hash(dict, key, &hash))
        return E_VB_INVALID;
    if (find_key(dict, key, hash))
        return E_VB_ELEMENTEXISTS;
    return add_node(dict, hash, key, item);
}

static HRESULT STDMETHODCALLTYPE dict_get_Count(IDictionary *This, LONG *count)
{
    if (!count)
        return E_POINTER;
    *count = dictionary_of(This)->Count;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE dict_Exists(IDictionary *This, VARIANT *key_in, VARIANT_BOOL *exists)
{
    DICTIONARY *dict = dictionary_of(This);
    VARIANT key_copy;
    VariantInit(&key_copy);
    VARIANT *key = strip_by_ref(&key_copy, key_in);
    if (!exists)
        return E_VB_INVALID;
    *exists = VARIANT_FALSE;
    ULONG hash;
    if (!key_hash(dict, key, &hash))
        return E_VB_INVALID;
    if (find_key(dict, key, hash))
        *exists = VARIANT_TRUE;
    return S_OK;
}

/* an array of the keys or of the items in the order they were added. */
static HRESULT dict_array(DICTIONARY *dict, VARIANT *out, BOOL keys)
{
    if (!out)
        return E_VB_INVALID;
    SAFEARRAYBOUND bound = {(ULONG)dict->Count, 0};
    SAFEARRAY *array = SafeArrayCreate(VT_VARIANT, 1, &bound);
    if (!array)
        return E_OUTOFMEMORY;
    if (FAILED(SafeArrayLock(array))) {
        SafeArrayDestroy(array);
        return E_VB_INVALID;
    }
    VARIANT *data = array->pvData;
    for (DICT_NODE *node = dict->First; node; node = node->Order) {
        HRESULT hr = VariantCopy(data++, keys ? &node->Key : &node->Item);
        if (FAILED(hr)) {
            SafeArrayUnlock(array);
            SafeArrayDestroy(array);
            return hr;
        }
    }
    SafeArrayUnlock(array);
    VariantClear(out);
    out->vt = VT_ARRAY | VT_VARIANT;
    out->parray = array;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE dict_Items(IDictionary *This, VARIANT *out)
{
    return dict_array(dictionary_of(This), out, FALSE);
}

static HRESULT STDMETHODCALLTYPE dict_Keys(IDictionary *This, VARIANT *out)
{
    if (!out)
        return S_OK;
    HRESULT hr = dict_array(dictionary_of(This), out, TRUE);
    return hr == E_OUTOFMEMORY || hr == E_VB_INVALID ? S_OK : hr;
}

static HRESULT STDMETHODCALLTYPE dict_put_Key(IDictionary *This, VARIANT *old_in, VARIANT *new_in)
{
    DICTIONARY *dict = dictionary_of(This);
    VARIANT old_copy, new_copy;
    VariantInit(&old_copy);
    VariantInit(&new_copy);
    VARIANT *old_key = strip_by_ref(&old_copy, old_in);
    VARIANT *new_key = strip_by_ref(&new_copy, new_in);
    ULONG old_hash, new_hash;
    if (!key_hash(dict, new_key, &new_hash) || !key_hash(dict, old_key, &old_hash))
        return E_VB_INVALID;
    DICT_NODE *previous = nullptr, *node = dict->Table[old_hash];
    while (node && !keys_equal(dict, old_key, &node->Key)) {
        previous = node;
        node = node->Chain;
    }
    if (!node)
        return E_VB_ELEMENTMISSING;
    if (keys_equal(dict, old_key, new_key))
        return S_OK;
    if (find_key(dict, new_key, new_hash))
        return E_VB_ELEMENTEXISTS;
    if (previous)
        previous->Chain = node->Chain;
    else
        dict->Table[old_hash] = node->Chain;
    node->Chain = dict->Table[new_hash];
    dict->Table[new_hash] = node;
    VariantClear(&node->Key);
    return VariantCopy(&node->Key, new_key);
}

static void enums_forget(DICTIONARY *dict, DICT_NODE *node)
{
    for (DICT_ENUM *e = dict->Enums; e; e = e->Next) {
        if (e->Current == node)
            e->Current = node->Order;
    }
}

static HRESULT STDMETHODCALLTYPE dict_Remove(IDictionary *This, VARIANT *key_in)
{
    DICTIONARY *dict = dictionary_of(This);
    VARIANT key_copy;
    VariantInit(&key_copy);
    VARIANT *key = strip_by_ref(&key_copy, key_in);
    ULONG hash;
    if (!key_hash(dict, key, &hash))
        return E_VB_INVALID;
    DICT_NODE *previous = nullptr, *node = dict->Table[hash];
    while (node && !keys_equal(dict, key, &node->Key)) {
        previous = node;
        node = node->Chain;
    }
    if (!node)
        return E_VB_ELEMENTMISSING;
    if (previous)
        previous->Chain = node->Chain;
    else
        dict->Table[hash] = node->Chain;
    enums_forget(dict, node);
    DICT_NODE *before = nullptr;
    for (DICT_NODE *walk = dict->First; walk; before = walk, walk = walk->Order) {
        if (walk != node)
            continue;
        if (before) {
            before->Order = node->Order;
            if (dict->Last == node)
                dict->Last = before;
        } else {
            dict->First = node->Order;
            if (!node->Order)
                dict->Last = nullptr;
        }
        break;
    }
    dict->Count--;
    free_node(node);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE dict_RemoveAll(IDictionary *This)
{
    DICTIONARY *dict = dictionary_of(This);
    for (DICT_ENUM *e = dict->Enums; e; e = e->Next)
        e->Current = nullptr;
    dict_remove_all_nodes(dict);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE dict_put_CompareMode(IDictionary *This, CompareMethod mode)
{
    DICTIONARY *dict = dictionary_of(This);
    if (dict->Count && dict->Requested != (LONG)mode)
        return E_VB_INVALID;
    ULONG value = (ULONG)mode;
    if (value > 1) {
        if (!value || CompareStringW(value, NORM_IGNORECASE | NORM_IGNORENONSPACE, L"Test", -1, L"Test", -1) != CSTR_EQUAL)
            return E_VB_INVALID;
        dict->Lcid = value;
        dict->Mode = TextCompare;
    } else {
        dict->Mode = (CompareMethod)value;
        dict->Lcid = GetUserDefaultLCID();
    }
    dict->Requested = (LONG)mode;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE dict_get_CompareMode(IDictionary *This, CompareMethod *mode)
{
    if (!mode)
        return E_POINTER;
    *mode = (CompareMethod)dictionary_of(This)->Requested;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE dict_get_NewEnum(IDictionary *This, IUnknown **out)
{
    DICTIONARY *dict = dictionary_of(This);
    DICT_ENUM *e = ScrAlloc(sizeof(*e));
    if (!e) {
        if (out)
            *out = nullptr;
        return E_OUTOFMEMORY;
    }
    e->Enum.lpVtbl = &g_enum_vtbl;
    e->Refs = 1;
    e->Dictionary = dict;
    e->Current = dict->First;
    e->Next = dict->Enums;
    dict->Enums = e;
    InterlockedIncrement(&dict->Refs);
    ScrLockDll();
    *out = (IUnknown *)&e->Enum;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE dict_get_HashVal(IDictionary *This, VARIANT *key_in, VARIANT *hash_out)
{
    DICTIONARY *dict = dictionary_of(This);
    VARIANT key_copy;
    VariantInit(&key_copy);
    VARIANT *key = strip_by_ref(&key_copy, key_in);
    hash_out->vt = VT_I4;
    ULONG hash;
    if (!key_hash(dict, key, &hash))
        return E_VB_INVALID;
    hash_out->lVal = (LONG)hash;
    return S_OK;
}

static const IDictionaryVtbl g_dictionary_vtbl = {
    dict_query_interface, dict_add_ref, dict_release, dict_GetTypeInfoCount, dict_GetTypeInfo, dict_GetIDsOfNames,
    dict_Invoke, dict_putref_Item, dict_put_Item, dict_get_Item, dict_Add, dict_get_Count, dict_Exists, dict_Items,
    dict_put_Key, dict_Keys, dict_Remove, dict_RemoveAll, dict_put_CompareMode, dict_get_CompareMode, dict_get_NewEnum,
    dict_get_HashVal,
};

/* ------------------------------------------------------------------ the other interfaces */

static HRESULT STDMETHODCALLTYPE info_query_interface(IProvideClassInfo *This, REFIID riid, void **object)
{
    return dict_query_interface(&OBJECT_OF(DICTIONARY, Info, This)->Dictionary, riid, object);
}

static ULONG STDMETHODCALLTYPE info_add_ref(IProvideClassInfo *This)
{
    return dict_add_ref(&OBJECT_OF(DICTIONARY, Info, This)->Dictionary);
}

static ULONG STDMETHODCALLTYPE info_release(IProvideClassInfo *This)
{
    return dict_release(&OBJECT_OF(DICTIONARY, Info, This)->Dictionary);
}

static HRESULT STDMETHODCALLTYPE info_get_class_info(IProvideClassInfo *This, ITypeInfo **info)
{
    return info ? ScrClassInfo(&CLSID_Dictionary, info) : E_POINTER;
}

static const IProvideClassInfoVtbl g_info_vtbl = {
    info_query_interface, info_add_ref, info_release, info_get_class_info,
};

static HRESULT STDMETHODCALLTYPE connection_query_interface(IExternalConnection *This, REFIID riid, void **object)
{
    return dict_query_interface(&OBJECT_OF(DICTIONARY, Connection, This)->Dictionary, riid, object);
}

static ULONG STDMETHODCALLTYPE connection_add_ref(IExternalConnection *This)
{
    return dict_add_ref(&OBJECT_OF(DICTIONARY, Connection, This)->Dictionary);
}

static ULONG STDMETHODCALLTYPE connection_release(IExternalConnection *This)
{
    return dict_release(&OBJECT_OF(DICTIONARY, Connection, This)->Dictionary);
}

static DWORD STDMETHODCALLTYPE connection_add(IExternalConnection *This, DWORD connection, DWORD reserved)
{
    return (DWORD)InterlockedIncrement(&OBJECT_OF(DICTIONARY, Connection, This)->Connections);
}

static DWORD STDMETHODCALLTYPE connection_remove(IExternalConnection *This, DWORD connection, DWORD reserved, BOOL last)
{
    return (DWORD)InterlockedDecrement(&OBJECT_OF(DICTIONARY, Connection, This)->Connections);
}

static const IExternalConnectionVtbl g_connection_vtbl = {
    connection_query_interface, connection_add_ref, connection_release, connection_add, connection_remove,
};

/* ------------------------------------------------------------------ the enumerator of keys */

static DICT_ENUM *enum_of(IEnumVARIANT *This)
{
    return (DICT_ENUM *)This;
}

static HRESULT STDMETHODCALLTYPE enum_query_interface(IEnumVARIANT *This, REFIID riid, void **object)
{
    if (!object)
        return E_POINTER;
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IEnumVARIANT)) {
        *object = This;
        InterlockedIncrement(&enum_of(This)->Refs);
        return S_OK;
    }
    *object = nullptr;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE enum_add_ref(IEnumVARIANT *This)
{
    return (ULONG)InterlockedIncrement(&enum_of(This)->Refs);
}

static ULONG STDMETHODCALLTYPE enum_release(IEnumVARIANT *This)
{
    DICT_ENUM *e = enum_of(This);
    LONG refs = InterlockedDecrement(&e->Refs);
    if (!refs) {
        DICTIONARY *dict = e->Dictionary;
        if (dict) {
            for (DICT_ENUM **link = &dict->Enums; *link; link = &(*link)->Next) {
                if (*link == e) {
                    *link = e->Next;
                    break;
                }
            }
        }
        ScrFree(e);
        ScrUnlockDll();
        if (dict)
            dict_release(&dict->Dictionary);
    }
    return (ULONG)refs;
}

static HRESULT STDMETHODCALLTYPE enum_next(IEnumVARIANT *This, ULONG count, VARIANT *values, ULONG *fetched)
{
    DICT_ENUM *e = enum_of(This);
    if (fetched)
        *fetched = 0;
    if (!e->Dictionary)
        return S_FALSE;
    ULONG got = 0;
    while (count && e->Current) {
        HRESULT hr = VariantCopy(&values[got], &e->Current->Key);
        if (FAILED(hr))
            return hr;
        e->Current = e->Current->Order;
        got++;
        count--;
    }
    if (fetched)
        *fetched = got;
    return count ? S_FALSE : S_OK;
}

static HRESULT STDMETHODCALLTYPE enum_skip(IEnumVARIANT *This, ULONG count)
{
    DICT_ENUM *e = enum_of(This);
    while (count && e->Current) {
        e->Current = e->Current->Order;
        count--;
    }
    return count ? S_FALSE : S_OK;
}

static HRESULT STDMETHODCALLTYPE enum_reset(IEnumVARIANT *This)
{
    DICT_ENUM *e = enum_of(This);
    if (e->Dictionary)
        e->Current = e->Dictionary->First;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE enum_clone(IEnumVARIANT *This, IEnumVARIANT **out)
{
    DICT_ENUM *e = enum_of(This);
    if (!out)
        return E_POINTER;
    *out = nullptr;
    if (!e->Dictionary)
        return E_FAIL;
    IUnknown *created;
    HRESULT hr = dict_get_NewEnum(&e->Dictionary->Dictionary, &created);
    if (FAILED(hr))
        return hr;
    enum_of((IEnumVARIANT *)created)->Current = e->Current;
    *out = (IEnumVARIANT *)created;
    return S_OK;
}

static const IEnumVARIANTVtbl g_enum_vtbl = {
    enum_query_interface, enum_add_ref, enum_release, enum_next, enum_skip, enum_reset, enum_clone,
};

HRESULT DictionaryCreate(REFIID riid, void **object)
{
    if (!IsEqualIID(riid, &IID_IUnknown) && !IsEqualIID(riid, &IID_IDispatch) && !IsEqualIID(riid, &IID_IDictionary))
        return E_NOINTERFACE;
    DICTIONARY *dict = ScrAlloc(sizeof(*dict));
    if (!dict)
        return E_OUTOFMEMORY;
    dict->Table = ScrAlloc(BUCKETS * sizeof(*dict->Table));
    if (!dict->Table) {
        ScrFree(dict);
        return E_OUTOFMEMORY;
    }
    dict->Dictionary.lpVtbl = &g_dictionary_vtbl;
    dict->Info.lpVtbl = &g_info_vtbl;
    dict->Connection.lpVtbl = &g_connection_vtbl;
    dict->Refs = 1;
    dict->Mode = BinaryCompare;
    dict->Lcid = GetUserDefaultLCID();
    ScrLockDll();
    *object = &dict->Dictionary;
    return S_OK;
}
