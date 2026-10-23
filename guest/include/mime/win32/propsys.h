/* property system: property keys, property stores and propsys.dll's PROPVARIANT and VARIANT helpers. */
#ifndef MIME_WIN32_PROPSYS_H
#define MIME_WIN32_PROPSYS_H

#include <mime/win32/objidl.h>
#include <mime/win32/propidl.h>

#ifndef PSSTDAPI
#define PSSTDAPI DECLSPEC_IMPORT HRESULT WINAPI
#endif
#ifndef PROPSYSAPI
#define PROPSYSAPI DECLSPEC_IMPORT
#endif

#ifndef DISP_E_OVERFLOW
#define DISP_E_OVERFLOW ((HRESULT)0x8002000AL)
#endif
#ifndef TYPE_E_ELEMENTNOTFOUND
#define TYPE_E_ELEMENTNOTFOUND ((HRESULT)0x8002802BL)
#endif
#ifndef TYPE_E_TYPEMISMATCH
#define TYPE_E_TYPEMISMATCH ((HRESULT)0x80028CA0L)
#endif
#ifndef STRSAFE_E_INSUFFICIENT_BUFFER
#define STRSAFE_E_INSUFFICIENT_BUFFER ((HRESULT)0x8007007AL)
#endif
#ifndef INPLACE_S_TRUNCATED
#define INPLACE_S_TRUNCATED ((HRESULT)0x000401A0L)
#endif

typedef struct _tagpropertykey {
    GUID fmtid;
    DWORD pid;
} PROPERTYKEY;
typedef const PROPERTYKEY *REFPROPERTYKEY;
typedef PROPERTYKEY SHCOLUMNID;

#define PKEY_PIDSTR_MAX 10
#define GUIDSTRING_MAX 39
#define PKEYSTR_MAX (GUIDSTRING_MAX + 1 + PKEY_PIDSTR_MAX)

#define MIME_DEFINE_PROPERTYKEY(name, l, w1, w2, b1, b2, b3, b4, b5, b6, b7, b8, pid)                              \
    const PROPERTYKEY DECLSPEC_SELECTANY name = {{l, w1, w2, {b1, b2, b3, b4, b5, b6, b7, b8}}, pid}

/* shell folder string results, from shtypes.h. */
#define STRRET_WSTR 0x0000
#define STRRET_OFFSET 0x0001
#define STRRET_CSTR 0x0002
typedef struct _STRRET {
    UINT uType;
    union {
        LPWSTR pOleStr;
        UINT uOffset;
        char cStr[260];
    };
} STRRET;
typedef struct _ITEMIDLIST ITEMIDLIST;
typedef const ITEMIDLIST *PCUITEMID_CHILD;

typedef enum PSC_STATE {
    PSC_NORMAL = 0,
    PSC_NOTINSOURCE = 1,
    PSC_DIRTY = 2,
    PSC_READONLY = 3,
} PSC_STATE;

typedef enum PERSIST_SPROPSTORE_FLAGS {
    FPSPS_DEFAULT = 0,
    FPSPS_READONLY = 0x1,
    FPSPS_TREAT_NEW_VALUES_AS_DIRTY = 0x2,
} PERSIST_SPROPSTORE_FLAGS;

typedef struct tagSERIALIZEDPROPSTORAGE SERIALIZEDPROPSTORAGE, *PUSERIALIZEDPROPSTORAGE;
typedef const SERIALIZEDPROPSTORAGE *PCUSERIALIZEDPROPSTORAGE;
typedef struct tagSERIALIZEDPROPERTYVALUE {
    DWORD dwType;
    BYTE rgb[1];
} SERIALIZEDPROPERTYVALUE;

typedef enum PROPVAR_CHANGE_FLAGS {
    PVCHF_DEFAULT = 0x00000000,
    PVCHF_NOVALUEPROP = 0x00000001,
    PVCHF_ALPHABOOL = 0x00000002,
    PVCHF_NOUSEROVERRIDE = 0x00000004,
    PVCHF_LOCALBOOL = 0x00000008,
    PVCHF_NOHEXSTRING = 0x00000010,
} PROPVAR_CHANGE_FLAGS;

typedef enum PROPVAR_COMPARE_UNIT {
    PVCU_DEFAULT = 0,
    PVCU_SECOND = 1,
    PVCU_MINUTE = 2,
    PVCU_HOUR = 3,
    PVCU_DAY = 4,
    PVCU_MONTH = 5,
    PVCU_YEAR = 6,
} PROPVAR_COMPARE_UNIT;

typedef enum PROPVAR_COMPARE_FLAGS {
    PVCF_DEFAULT = 0x00000000,
    PVCF_TREATEMPTYASGREATERTHAN = 0x00000001,
    PVCF_USESTRCMP = 0x00000002,
    PVCF_USESTRCMPC = 0x00000004,
    PVCF_USESTRCMPI = 0x00000008,
    PVCF_USESTRCMPIC = 0x00000010,
    PVCF_DIGITSASNUMBERS_CASESENSITIVE = 0x00000020,
} PROPVAR_COMPARE_FLAGS;

/* ------------------------------------------------------------------ interfaces */

typedef struct IPropertyStore IPropertyStore;
typedef struct IPropertyStoreVtbl {
    HRESULT(STDMETHODCALLTYPE *QueryInterface)(IPropertyStore *This, REFIID riid, void **object);
    ULONG(STDMETHODCALLTYPE *AddRef)(IPropertyStore *This);
    ULONG(STDMETHODCALLTYPE *Release)(IPropertyStore *This);
    HRESULT(STDMETHODCALLTYPE *GetCount)(IPropertyStore *This, DWORD *count);
    HRESULT(STDMETHODCALLTYPE *GetAt)(IPropertyStore *This, DWORD index, PROPERTYKEY *key);
    HRESULT(STDMETHODCALLTYPE *GetValue)(IPropertyStore *This, REFPROPERTYKEY key, PROPVARIANT *value);
    HRESULT(STDMETHODCALLTYPE *SetValue)(IPropertyStore *This, REFPROPERTYKEY key, const PROPVARIANT *value);
    HRESULT(STDMETHODCALLTYPE *Commit)(IPropertyStore *This);
} IPropertyStoreVtbl;
struct IPropertyStore {
    const IPropertyStoreVtbl *lpVtbl;
};
MIME_DEFINE_GUID(IID_IPropertyStore, 0x886d8eeb, 0x8cf2, 0x4446, 0x8d, 0x02, 0xcd, 0xba, 0x1d, 0xbd, 0xcf, 0x99);

typedef struct IPropertyStoreCache IPropertyStoreCache;
typedef struct IPropertyStoreCacheVtbl {
    HRESULT(STDMETHODCALLTYPE *QueryInterface)(IPropertyStoreCache *This, REFIID riid, void **object);
    ULONG(STDMETHODCALLTYPE *AddRef)(IPropertyStoreCache *This);
    ULONG(STDMETHODCALLTYPE *Release)(IPropertyStoreCache *This);
    HRESULT(STDMETHODCALLTYPE *GetCount)(IPropertyStoreCache *This, DWORD *count);
    HRESULT(STDMETHODCALLTYPE *GetAt)(IPropertyStoreCache *This, DWORD index, PROPERTYKEY *key);
    HRESULT(STDMETHODCALLTYPE *GetValue)(IPropertyStoreCache *This, REFPROPERTYKEY key, PROPVARIANT *value);
    HRESULT(STDMETHODCALLTYPE *SetValue)(IPropertyStoreCache *This, REFPROPERTYKEY key, const PROPVARIANT *value);
    HRESULT(STDMETHODCALLTYPE *Commit)(IPropertyStoreCache *This);
    HRESULT(STDMETHODCALLTYPE *GetState)(IPropertyStoreCache *This, REFPROPERTYKEY key, PSC_STATE *state);
    HRESULT(STDMETHODCALLTYPE *GetValueAndState)(IPropertyStoreCache *This, REFPROPERTYKEY key, PROPVARIANT *value,
                                                 PSC_STATE *state);
    HRESULT(STDMETHODCALLTYPE *SetState)(IPropertyStoreCache *This, REFPROPERTYKEY key, PSC_STATE state);
    HRESULT(STDMETHODCALLTYPE *SetValueAndState)(IPropertyStoreCache *This, REFPROPERTYKEY key,
                                                 const PROPVARIANT *value, PSC_STATE state);
} IPropertyStoreCacheVtbl;
struct IPropertyStoreCache {
    const IPropertyStoreCacheVtbl *lpVtbl;
};
MIME_DEFINE_GUID(IID_IPropertyStoreCache, 0x3017056d, 0x9a91, 0x4e90, 0x93, 0x7d, 0x74, 0x6c, 0x72, 0xab, 0xbf, 0x4f);

typedef struct INamedPropertyStore INamedPropertyStore;
typedef struct INamedPropertyStoreVtbl {
    HRESULT(STDMETHODCALLTYPE *QueryInterface)(INamedPropertyStore *This, REFIID riid, void **object);
    ULONG(STDMETHODCALLTYPE *AddRef)(INamedPropertyStore *This);
    ULONG(STDMETHODCALLTYPE *Release)(INamedPropertyStore *This);
    HRESULT(STDMETHODCALLTYPE *GetNamedValue)(INamedPropertyStore *This, LPCWSTR name, PROPVARIANT *value);
    HRESULT(STDMETHODCALLTYPE *SetNamedValue)(INamedPropertyStore *This, LPCWSTR name, const PROPVARIANT *value);
    HRESULT(STDMETHODCALLTYPE *GetNameCount)(INamedPropertyStore *This, DWORD *count);
    HRESULT(STDMETHODCALLTYPE *GetNameAt)(INamedPropertyStore *This, DWORD index, BSTR *name);
} INamedPropertyStoreVtbl;
struct INamedPropertyStore {
    const INamedPropertyStoreVtbl *lpVtbl;
};
MIME_DEFINE_GUID(IID_INamedPropertyStore, 0x71604b0f, 0x97b0, 0x4764, 0x85, 0x77, 0x2f, 0x13, 0xe9, 0x8a, 0x14, 0x22);

typedef struct IPersistSerializedPropStorage IPersistSerializedPropStorage;
typedef struct IPersistSerializedPropStorageVtbl {
    HRESULT(STDMETHODCALLTYPE *QueryInterface)(IPersistSerializedPropStorage *This, REFIID riid, void **object);
    ULONG(STDMETHODCALLTYPE *AddRef)(IPersistSerializedPropStorage *This);
    ULONG(STDMETHODCALLTYPE *Release)(IPersistSerializedPropStorage *This);
    HRESULT(STDMETHODCALLTYPE *SetFlags)(IPersistSerializedPropStorage *This, PERSIST_SPROPSTORE_FLAGS flags);
    HRESULT(STDMETHODCALLTYPE *SetPropertyStorage)(IPersistSerializedPropStorage *This,
                                                   PCUSERIALIZEDPROPSTORAGE storage, DWORD size);
    HRESULT(STDMETHODCALLTYPE *GetPropertyStorage)(IPersistSerializedPropStorage *This,
                                                   SERIALIZEDPROPSTORAGE **storage, DWORD *size);
    /* IPersistSerializedPropStorage2. */
    HRESULT(STDMETHODCALLTYPE *GetPropertyStorageSize)(IPersistSerializedPropStorage *This, DWORD *size);
    HRESULT(STDMETHODCALLTYPE *GetPropertyStorageBuffer)(IPersistSerializedPropStorage *This,
                                                         SERIALIZEDPROPSTORAGE *storage, DWORD size, DWORD *written);
} IPersistSerializedPropStorageVtbl;
struct IPersistSerializedPropStorage {
    const IPersistSerializedPropStorageVtbl *lpVtbl;
};
typedef IPersistSerializedPropStorage IPersistSerializedPropStorage2;
MIME_DEFINE_GUID(IID_IPersistSerializedPropStorage, 0xe318ad57, 0x0aa0, 0x450f, 0xac, 0xa5, 0x6f, 0xab, 0x71, 0x03,
                 0xd9, 0x17);
MIME_DEFINE_GUID(IID_IPersistSerializedPropStorage2, 0x77effa68, 0x4f98, 0x4366, 0xba, 0x72, 0x57, 0x3b, 0x3d, 0x88,
                 0x05, 0x71);

typedef struct IErrorLog IErrorLog;
typedef struct IPropertyBag IPropertyBag;
typedef struct IPropertyBagVtbl {
    HRESULT(STDMETHODCALLTYPE *QueryInterface)(IPropertyBag *This, REFIID riid, void **object);
    ULONG(STDMETHODCALLTYPE *AddRef)(IPropertyBag *This);
    ULONG(STDMETHODCALLTYPE *Release)(IPropertyBag *This);
    HRESULT(STDMETHODCALLTYPE *Read)(IPropertyBag *This, LPCOLESTR name, VARIANT *value, IErrorLog *log);
    HRESULT(STDMETHODCALLTYPE *Write)(IPropertyBag *This, LPCOLESTR name, VARIANT *value);
} IPropertyBagVtbl;
struct IPropertyBag {
    const IPropertyBagVtbl *lpVtbl;
};
MIME_DEFINE_GUID(IID_IPropertyBag, 0x55272a00, 0x42cb, 0x11ce, 0x81, 0x35, 0x00, 0xaa, 0x00, 0x4b, 0xb8, 0x51);
MIME_DEFINE_GUID(IID_IAgileObject, 0x94ea2b94, 0xe9cc, 0x49e0, 0xc0, 0xff, 0xee, 0x64, 0xca, 0x8f, 0x5b, 0x90);

MIME_DEFINE_PROPERTYKEY(PKEY_AppUserModel_RelaunchCommand, 0x9f4c2855, 0x9f79, 0x4b39, 0xa8, 0xd0, 0xe1, 0xd4, 0x2d,
                        0xe1, 0xd5, 0xf3, 2);
MIME_DEFINE_PROPERTYKEY(PKEY_AppUserModel_RelaunchIconResource, 0x9f4c2855, 0x9f79, 0x4b39, 0xa8, 0xd0, 0xe1, 0xd4,
                        0x2d, 0xe1, 0xd5, 0xf3, 3);
MIME_DEFINE_PROPERTYKEY(PKEY_AppUserModel_RelaunchDisplayNameResource, 0x9f4c2855, 0x9f79, 0x4b39, 0xa8, 0xd0, 0xe1,
                        0xd4, 0x2d, 0xe1, 0xd5, 0xf3, 4);
MIME_DEFINE_PROPERTYKEY(PKEY_AppUserModel_ID, 0x9f4c2855, 0x9f79, 0x4b39, 0xa8, 0xd0, 0xe1, 0xd4, 0x2d, 0xe1, 0xd5,
                        0xf3, 5);
MIME_DEFINE_PROPERTYKEY(PKEY_AppUserModel_PreventPinning, 0x9f4c2855, 0x9f79, 0x4b39, 0xa8, 0xd0, 0xe1, 0xd4, 0x2d,
                        0xe1, 0xd5, 0xf3, 9);
MIME_DEFINE_PROPERTYKEY(PKEY_AppUserModel_IsDualMode, 0x9f4c2855, 0x9f79, 0x4b39, 0xa8, 0xd0, 0xe1, 0xd4, 0x2d, 0xe1,
                        0xd5, 0xf3, 11);
MIME_DEFINE_PROPERTYKEY(PKEY_AppUserModel_ToastActivatorCLSID, 0x9f4c2855, 0x9f79, 0x4b39, 0xa8, 0xd0, 0xe1, 0xd4,
                        0x2d, 0xe1, 0xd5, 0xf3, 26);
MIME_DEFINE_PROPERTYKEY(PKEY_Title, 0xf29f85e0, 0x4ff9, 0x1068, 0xab, 0x91, 0x08, 0x00, 0x2b, 0x27, 0xb3, 0xd9, 2);
MIME_DEFINE_PROPERTYKEY(PKEY_ItemNameDisplay, 0xb725f130, 0x47ef, 0x101a, 0xa5, 0xf1, 0x02, 0x60, 0x8c, 0x9e, 0xeb,
                        0xac, 10);

/* ------------------------------------------------------------------ propsys.dll */

PSSTDAPI PSCreateMemoryPropertyStore(REFIID riid, void **object);
PSSTDAPI PSGetPropertyKeyFromName(LPCWSTR name, PROPERTYKEY *key);
PSSTDAPI PSGetNameFromPropertyKey(REFPROPERTYKEY key, LPWSTR *name);
PSSTDAPI PSStringFromPropertyKey(REFPROPERTYKEY key, LPWSTR string, UINT length);
PSSTDAPI PSPropertyKeyFromString(LPCWSTR string, PROPERTYKEY *key);
PSSTDAPI PSFormatForDisplayAlloc(REFPROPERTYKEY key, const PROPVARIANT *value, DWORD flags, LPWSTR *text);
PSSTDAPI PSPropertyBag_ReadStrAlloc(IPropertyBag *bag, LPCWSTR name, LPWSTR *value);
PSSTDAPI PSPropertyBag_WriteStr(IPropertyBag *bag, LPCWSTR name, LPCWSTR value);
PSSTDAPI PSPropertyBag_ReadDWORD(IPropertyBag *bag, LPCWSTR name, DWORD *value);
PSSTDAPI PSPropertyBag_WriteDWORD(IPropertyBag *bag, LPCWSTR name, DWORD value);
PSSTDAPI PSPropertyBag_Delete(IPropertyBag *bag, LPCWSTR name);
PSSTDAPI PSGetPropertyFromPropertyStorage(PCUSERIALIZEDPROPSTORAGE storage, DWORD size, REFPROPERTYKEY key,
                                          PROPVARIANT *value);
PSSTDAPI PSGetNamedPropertyFromPropertyStorage(PCUSERIALIZEDPROPSTORAGE storage, DWORD size, LPCWSTR name,
                                               PROPVARIANT *value);
PSSTDAPI StgSerializePropVariant(const PROPVARIANT *value, SERIALIZEDPROPERTYVALUE **out, ULONG *size);
PSSTDAPI StgDeserializePropVariant(const SERIALIZEDPROPERTYVALUE *in, ULONG size, PROPVARIANT *value);

PSSTDAPI InitPropVariantFromResource(HINSTANCE instance, UINT id, PROPVARIANT *value);
PSSTDAPI InitPropVariantFromBuffer(const void *buffer, UINT size, PROPVARIANT *value);
PSSTDAPI InitPropVariantFromCLSID(REFCLSID clsid, PROPVARIANT *value);
PSSTDAPI InitPropVariantFromGUIDAsString(REFGUID guid, PROPVARIANT *value);
PSSTDAPI InitPropVariantFromFileTime(const FILETIME *time, PROPVARIANT *value);
PSSTDAPI InitPropVariantFromStrRet(STRRET *strret, PCUITEMID_CHILD pidl, PROPVARIANT *value);
PSSTDAPI InitPropVariantFromBooleanVector(const BOOL *values, ULONG count, PROPVARIANT *value);
PSSTDAPI InitPropVariantFromInt16Vector(const SHORT *values, ULONG count, PROPVARIANT *value);
PSSTDAPI InitPropVariantFromUInt16Vector(const USHORT *values, ULONG count, PROPVARIANT *value);
PSSTDAPI InitPropVariantFromInt32Vector(const LONG *values, ULONG count, PROPVARIANT *value);
PSSTDAPI InitPropVariantFromUInt32Vector(const ULONG *values, ULONG count, PROPVARIANT *value);
PSSTDAPI InitPropVariantFromInt64Vector(const LONGLONG *values, ULONG count, PROPVARIANT *value);
PSSTDAPI InitPropVariantFromUInt64Vector(const ULONGLONG *values, ULONG count, PROPVARIANT *value);
PSSTDAPI InitPropVariantFromDoubleVector(const DOUBLE *values, ULONG count, PROPVARIANT *value);
PSSTDAPI InitPropVariantFromFileTimeVector(const FILETIME *values, ULONG count, PROPVARIANT *value);
PSSTDAPI InitPropVariantFromStringVector(LPCWSTR *values, ULONG count, PROPVARIANT *value);
PSSTDAPI InitPropVariantFromStringAsVector(LPCWSTR string, PROPVARIANT *value);
PSSTDAPI InitPropVariantFromPropVariantVectorElem(const PROPVARIANT *source, ULONG index, PROPVARIANT *value);
PSSTDAPI InitPropVariantVectorFromPropVariant(const PROPVARIANT *source, PROPVARIANT *value);

PROPSYSAPI BOOL WINAPI PropVariantToBooleanWithDefault(const PROPVARIANT *value, BOOL fallback);
PROPSYSAPI SHORT WINAPI PropVariantToInt16WithDefault(const PROPVARIANT *value, SHORT fallback);
PROPSYSAPI USHORT WINAPI PropVariantToUInt16WithDefault(const PROPVARIANT *value, USHORT fallback);
PROPSYSAPI LONG WINAPI PropVariantToInt32WithDefault(const PROPVARIANT *value, LONG fallback);
PROPSYSAPI ULONG WINAPI PropVariantToUInt32WithDefault(const PROPVARIANT *value, ULONG fallback);
PROPSYSAPI LONGLONG WINAPI PropVariantToInt64WithDefault(const PROPVARIANT *value, LONGLONG fallback);
PROPSYSAPI ULONGLONG WINAPI PropVariantToUInt64WithDefault(const PROPVARIANT *value, ULONGLONG fallback);
PROPSYSAPI DOUBLE WINAPI PropVariantToDoubleWithDefault(const PROPVARIANT *value, DOUBLE fallback);
PROPSYSAPI LPCWSTR WINAPI PropVariantToStringWithDefault(const PROPVARIANT *value, LPCWSTR fallback);
PSSTDAPI PropVariantToBoolean(const PROPVARIANT *value, BOOL *out);
PSSTDAPI PropVariantToInt16(const PROPVARIANT *value, SHORT *out);
PSSTDAPI PropVariantToUInt16(const PROPVARIANT *value, USHORT *out);
PSSTDAPI PropVariantToInt32(const PROPVARIANT *value, LONG *out);
PSSTDAPI PropVariantToUInt32(const PROPVARIANT *value, ULONG *out);
PSSTDAPI PropVariantToInt64(const PROPVARIANT *value, LONGLONG *out);
PSSTDAPI PropVariantToUInt64(const PROPVARIANT *value, ULONGLONG *out);
PSSTDAPI PropVariantToDouble(const PROPVARIANT *value, DOUBLE *out);
PSSTDAPI PropVariantToBuffer(const PROPVARIANT *value, void *buffer, UINT size);
PSSTDAPI PropVariantToString(const PROPVARIANT *value, LPWSTR buffer, UINT length);
PSSTDAPI PropVariantToGUID(const PROPVARIANT *value, GUID *out);
PSSTDAPI PropVariantToStringAlloc(const PROPVARIANT *value, LPWSTR *out);
PSSTDAPI PropVariantToBSTR(const PROPVARIANT *value, BSTR *out);
PSSTDAPI PropVariantToStrRet(const PROPVARIANT *value, STRRET *out);
PSSTDAPI PropVariantToFileTime(const PROPVARIANT *value, DWORD flags, FILETIME *out);
PSSTDAPI PropVariantToStringVectorAlloc(const PROPVARIANT *value, LPWSTR **out, ULONG *count);
PSSTDAPI PropVariantToInt32VectorAlloc(const PROPVARIANT *value, LONG **out, ULONG *count);
PSSTDAPI PropVariantToInt32Vector(const PROPVARIANT *value, LONG *out, ULONG size, ULONG *count);
PROPSYSAPI ULONG WINAPI PropVariantGetElementCount(const PROPVARIANT *value);
PSSTDAPI PropVariantGetStringElem(const PROPVARIANT *value, ULONG index, LPWSTR *out);
PSSTDAPI PropVariantGetInt32Elem(const PROPVARIANT *value, ULONG index, LONG *out);
PSSTDAPI PropVariantChangeType(PROPVARIANT *dest, const PROPVARIANT *source, PROPVAR_CHANGE_FLAGS flags, VARTYPE type);
PROPSYSAPI INT WINAPI PropVariantCompareEx(const PROPVARIANT *a, const PROPVARIANT *b, PROPVAR_COMPARE_UNIT unit,
                                           PROPVAR_COMPARE_FLAGS flags);
PSSTDAPI PropVariantToVariant(const PROPVARIANT *source, VARIANT *dest);
PSSTDAPI VariantToPropVariant(const VARIANT *source, PROPVARIANT *dest);

PSSTDAPI InitVariantFromResource(HINSTANCE instance, UINT id, VARIANT *value);
PSSTDAPI InitVariantFromBuffer(const void *buffer, UINT size, VARIANT *value);
PSSTDAPI InitVariantFromGUIDAsString(REFGUID guid, VARIANT *value);
PSSTDAPI InitVariantFromFileTime(const FILETIME *time, VARIANT *value);
PSSTDAPI InitVariantFromInt32Array(const LONG *values, ULONG count, VARIANT *value);
PSSTDAPI InitVariantFromStringArray(LPCWSTR *values, ULONG count, VARIANT *value);
PSSTDAPI InitVariantFromVariantArrayElem(const VARIANT *source, ULONG index, VARIANT *value);
PROPSYSAPI ULONG WINAPI VariantGetElementCount(const VARIANT *value);
PSSTDAPI VariantToInt32(const VARIANT *value, LONG *out);
PSSTDAPI VariantToBoolean(const VARIANT *value, BOOL *out);
PSSTDAPI VariantToString(const VARIANT *value, LPWSTR buffer, UINT length);
PSSTDAPI VariantToStringAlloc(const VARIANT *value, LPWSTR *out);
PSSTDAPI VariantToGUID(const VARIANT *value, GUID *out);
PSSTDAPI VariantToBuffer(const VARIANT *value, void *buffer, UINT size);
PROPSYSAPI INT WINAPI VariantCompare(const VARIANT *a, const VARIANT *b);

#endif
