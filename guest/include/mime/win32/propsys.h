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
#ifndef MIME_HAVE_IID_IAGILEOBJECT
#define MIME_HAVE_IID_IAGILEOBJECT
MIME_DEFINE_GUID(IID_IAgileObject, 0x94ea2b94, 0xe9cc, 0x49e0, 0xc0, 0xff, 0xee, 0x64, 0xca, 0x8f, 0x5b, 0x90);
#endif

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


/* ------------------------------------------------------------------ the property schema */

typedef DWORD SHCOLSTATEF;
typedef const PROPVARIANT *REFPROPVARIANT;
typedef struct IShellItem IShellItem;
typedef struct IPropertySetStorage IPropertySetStorage;

typedef enum PROPDESC_TYPE_FLAGS {
    PDTF_DEFAULT = 0,
    PDTF_MULTIPLEVALUES = 0x1,
    PDTF_ISINNATE = 0x2,
    PDTF_ISGROUP = 0x4,
    PDTF_CANGROUPBY = 0x8,
    PDTF_CANSTACKBY = 0x10,
    PDTF_ISTREEPROPERTY = 0x20,
    PDTF_INCLUDEINFULLTEXTQUERY = 0x40,
    PDTF_ISVIEWABLE = 0x80,
    PDTF_ISQUERYABLE = 0x100,
    PDTF_CANBEPURGED = 0x200,
    PDTF_SEARCHRAWVALUE = 0x400,
    PDTF_DONTCOERCEEMPTYSTRINGS = 0x800,
    PDTF_ALWAYSINSUPPLEMENTALSTORE = 0x1000,
    PDTF_ISSYSTEMPROPERTY = 0x80000000,
    PDTF_MASK_ALL = 0x80001FFF
} PROPDESC_TYPE_FLAGS;

typedef enum PROPDESC_VIEW_FLAGS {
    PDVF_DEFAULT = 0,
    PDVF_CENTERALIGN = 0x1,
    PDVF_RIGHTALIGN = 0x2,
    PDVF_BEGINNEWGROUP = 0x4,
    PDVF_FILLAREA = 0x8,
    PDVF_SORTDESCENDING = 0x10,
    PDVF_SHOWONLYIFPRESENT = 0x20,
    PDVF_SHOWBYDEFAULT = 0x40,
    PDVF_SHOWINPRIMARYLIST = 0x80,
    PDVF_SHOWINSECONDARYLIST = 0x100,
    PDVF_HIDELABEL = 0x200,
    PDVF_HIDDEN = 0x800,
    PDVF_CANWRAP = 0x1000,
    PDVF_MASK_ALL = 0x1BFF
} PROPDESC_VIEW_FLAGS;

typedef enum PROPDESC_DISPLAYTYPE {
    PDDT_STRING = 0,
    PDDT_NUMBER = 1,
    PDDT_BOOLEAN = 2,
    PDDT_DATETIME = 3,
    PDDT_ENUMERATED = 4
} PROPDESC_DISPLAYTYPE;

typedef enum PROPDESC_GROUPING_RANGE {
    PDGR_DISCRETE = 0,
    PDGR_ALPHANUMERIC = 1,
    PDGR_SIZE = 2,
    PDGR_DYNAMIC = 3,
    PDGR_DATE = 4,
    PDGR_PERCENT = 5,
    PDGR_ENUMERATED = 6
} PROPDESC_GROUPING_RANGE;

typedef enum PROPDESC_FORMAT_FLAGS {
    PDFF_DEFAULT = 0,
    PDFF_PREFIXNAME = 0x1,
    PDFF_FILENAME = 0x2,
    PDFF_ALWAYSKB = 0x4,
    PDFF_RESERVED_RIGHTTOLEFT = 0x8,
    PDFF_SHORTTIME = 0x10,
    PDFF_LONGTIME = 0x20,
    PDFF_HIDETIME = 0x40,
    PDFF_SHORTDATE = 0x80,
    PDFF_LONGDATE = 0x100,
    PDFF_HIDEDATE = 0x200,
    PDFF_RELATIVEDATE = 0x400,
    PDFF_USEEDITINVITATION = 0x800,
    PDFF_READONLY = 0x1000,
    PDFF_NOAUTOREADINGORDER = 0x2000
} PROPDESC_FORMAT_FLAGS;

typedef enum PROPDESC_SORTDESCRIPTION {
    PDSD_GENERAL = 0,
    PDSD_A_Z = 1,
    PDSD_LOWEST_HIGHEST = 2,
    PDSD_SMALLEST_BIGGEST = 3,
    PDSD_OLDEST_NEWEST = 4
} PROPDESC_SORTDESCRIPTION;

typedef enum PROPDESC_RELATIVEDESCRIPTION_TYPE {
    PDRDT_GENERAL = 0,
    PDRDT_DATE = 1,
    PDRDT_SIZE = 2,
    PDRDT_COUNT = 3,
    PDRDT_REVISION = 4,
    PDRDT_LENGTH = 5,
    PDRDT_DURATION = 6,
    PDRDT_SPEED = 7,
    PDRDT_RATE = 8,
    PDRDT_RATING = 9,
    PDRDT_PRIORITY = 10
} PROPDESC_RELATIVEDESCRIPTION_TYPE;

typedef enum PROPDESC_AGGREGATION_TYPE {
    PDAT_DEFAULT = 0,
    PDAT_FIRST = 1,
    PDAT_SUM = 2,
    PDAT_AVERAGE = 3,
    PDAT_DATERANGE = 4,
    PDAT_UNION = 5,
    PDAT_MAX = 6,
    PDAT_MIN = 7
} PROPDESC_AGGREGATION_TYPE;

typedef enum PROPDESC_CONDITION_TYPE {
    PDCOT_NONE = 0,
    PDCOT_STRING = 1,
    PDCOT_SIZE = 2,
    PDCOT_DATETIME = 3,
    PDCOT_BOOLEAN = 4,
    PDCOT_NUMBER = 5
} PROPDESC_CONDITION_TYPE;

typedef enum tagCONDITION_OPERATION {
    COP_IMPLICIT = 0,
    COP_EQUAL = 1,
    COP_NOTEQUAL = 2,
    COP_LESSTHAN = 3,
    COP_GREATERTHAN = 4,
    COP_LESSTHANOREQUAL = 5,
    COP_GREATERTHANOREQUAL = 6,
    COP_VALUE_STARTSWITH = 7,
    COP_VALUE_ENDSWITH = 8,
    COP_VALUE_CONTAINS = 9,
    COP_VALUE_NOTCONTAINS = 10,
    COP_DOSWILDCARDS = 11,
    COP_WORD_EQUAL = 12,
    COP_WORD_STARTSWITH = 13,
    COP_APPLICATION_SPECIFIC = 14
} CONDITION_OPERATION;

typedef enum PROPDESC_ENUMFILTER {
    PDEF_ALL = 0,
    PDEF_SYSTEM = 1,
    PDEF_NONSYSTEM = 2,
    PDEF_VIEWABLE = 3,
    PDEF_QUERYABLE = 4,
    PDEF_INFULLTEXTQUERY = 5,
    PDEF_COLUMN = 6
} PROPDESC_ENUMFILTER;

typedef enum PROPDESC_SEARCHINFO_FLAGS {
    PDSIF_DEFAULT = 0,
    PDSIF_ININVERTEDINDEX = 0x1,
    PDSIF_ISCOLUMN = 0x2,
    PDSIF_ISCOLUMNSPARSE = 0x4,
    PDSIF_ALWAYSINCLUDE = 0x8,
    PDSIF_USEFORTYPEAHEAD = 0x10
} PROPDESC_SEARCHINFO_FLAGS;

typedef enum PROPDESC_COLUMNINDEX_TYPE {
    PDCIT_NONE = 0,
    PDCIT_ONDISK = 1,
    PDCIT_INMEMORY = 2,
    PDCIT_ONDEMAND = 3,
    PDCIT_ONDISKALL = 4,
    PDCIT_ONDISKVECTOR = 5
} PROPDESC_COLUMNINDEX_TYPE;

typedef enum tagPROPENUMTYPE {
    PET_DISCRETEVALUE = 0,
    PET_RANGEDVALUE = 1,
    PET_DEFAULTVALUE = 2,
    PET_ENDRANGE = 3
} PROPENUMTYPE;

typedef int GETPROPERTYSTOREFLAGS;
#define GPS_DEFAULT 0
#define GPS_HANDLERPROPERTIESONLY 0x1
#define GPS_READWRITE 0x2
#define GPS_TEMPORARY 0x4
#define GPS_FASTPROPERTIESONLY 0x8
#define GPS_OPENSLOWITEM 0x10
#define GPS_DELAYCREATION 0x20
#define GPS_BESTEFFORT 0x40
#define GPS_NO_OPLOCK 0x80
#define GPS_PREFERQUERYPROPERTIES 0x100
#define GPS_EXTRINSICPROPERTIES 0x200
#define GPS_EXTRINSICPROPERTIESONLY 0x400
#define GPS_MASK_VALID 0x7FF

typedef enum _PKA_FLAGS { PKA_SET = 0, PKA_APPEND = 1, PKA_DELETE = 2 } PKA_FLAGS;

typedef struct IPropertyDescription IPropertyDescription;
typedef struct IPropertyDescriptionVtbl {
    HRESULT(STDMETHODCALLTYPE *QueryInterface)(IPropertyDescription *This, REFIID riid, void **object);
    ULONG(STDMETHODCALLTYPE *AddRef)(IPropertyDescription *This);
    ULONG(STDMETHODCALLTYPE *Release)(IPropertyDescription *This);
    HRESULT(STDMETHODCALLTYPE *GetPropertyKey)(IPropertyDescription *This, PROPERTYKEY *key);
    HRESULT(STDMETHODCALLTYPE *GetCanonicalName)(IPropertyDescription *This, LPWSTR *name);
    HRESULT(STDMETHODCALLTYPE *GetPropertyType)(IPropertyDescription *This, VARTYPE *type);
    HRESULT(STDMETHODCALLTYPE *GetDisplayName)(IPropertyDescription *This, LPWSTR *name);
    HRESULT(STDMETHODCALLTYPE *GetEditInvitation)(IPropertyDescription *This, LPWSTR *text);
    HRESULT(STDMETHODCALLTYPE *GetTypeFlags)(IPropertyDescription *This, PROPDESC_TYPE_FLAGS mask, PROPDESC_TYPE_FLAGS *flags);
    HRESULT(STDMETHODCALLTYPE *GetViewFlags)(IPropertyDescription *This, PROPDESC_VIEW_FLAGS *flags);
    HRESULT(STDMETHODCALLTYPE *GetDefaultColumnWidth)(IPropertyDescription *This, UINT *width);
    HRESULT(STDMETHODCALLTYPE *GetDisplayType)(IPropertyDescription *This, PROPDESC_DISPLAYTYPE *type);
    HRESULT(STDMETHODCALLTYPE *GetColumnState)(IPropertyDescription *This, SHCOLSTATEF *state);
    HRESULT(STDMETHODCALLTYPE *GetGroupingRange)(IPropertyDescription *This, PROPDESC_GROUPING_RANGE *range);
    HRESULT(STDMETHODCALLTYPE *GetRelativeDescriptionType)(IPropertyDescription *This, PROPDESC_RELATIVEDESCRIPTION_TYPE *type);
    HRESULT(STDMETHODCALLTYPE *GetRelativeDescription)(IPropertyDescription *This, REFPROPVARIANT first, REFPROPVARIANT second, LPWSTR *a, LPWSTR *b);
    HRESULT(STDMETHODCALLTYPE *GetSortDescription)(IPropertyDescription *This, PROPDESC_SORTDESCRIPTION *sort);
    HRESULT(STDMETHODCALLTYPE *GetSortDescriptionLabel)(IPropertyDescription *This, BOOL descending, LPWSTR *label);
    HRESULT(STDMETHODCALLTYPE *GetAggregationType)(IPropertyDescription *This, PROPDESC_AGGREGATION_TYPE *type);
    HRESULT(STDMETHODCALLTYPE *GetConditionType)(IPropertyDescription *This, PROPDESC_CONDITION_TYPE *type, CONDITION_OPERATION *operation);
    HRESULT(STDMETHODCALLTYPE *GetEnumTypeList)(IPropertyDescription *This, REFIID riid, void **object);
    HRESULT(STDMETHODCALLTYPE *CoerceToCanonicalValue)(IPropertyDescription *This, PROPVARIANT *value);
    HRESULT(STDMETHODCALLTYPE *FormatForDisplay)(IPropertyDescription *This, REFPROPVARIANT value, PROPDESC_FORMAT_FLAGS flags, LPWSTR *text);
    HRESULT(STDMETHODCALLTYPE *IsValueCanonical)(IPropertyDescription *This, REFPROPVARIANT value);
} IPropertyDescriptionVtbl;
struct IPropertyDescription {
    const IPropertyDescriptionVtbl *lpVtbl;
};
MIME_DEFINE_GUID(IID_IPropertyDescription, 0x6F79D558, 0x3E96, 0x4549, 0xA1, 0xD1, 0x7D, 0x75, 0xD2, 0x28, 0x88, 0x14);

typedef struct IPropertyDescription2 IPropertyDescription2;
typedef struct IPropertyDescription2Vtbl {
    HRESULT(STDMETHODCALLTYPE *QueryInterface)(IPropertyDescription2 *This, REFIID riid, void **object);
    ULONG(STDMETHODCALLTYPE *AddRef)(IPropertyDescription2 *This);
    ULONG(STDMETHODCALLTYPE *Release)(IPropertyDescription2 *This);
    HRESULT(STDMETHODCALLTYPE *GetPropertyKey)(IPropertyDescription2 *This, PROPERTYKEY *key);
    HRESULT(STDMETHODCALLTYPE *GetCanonicalName)(IPropertyDescription2 *This, LPWSTR *name);
    HRESULT(STDMETHODCALLTYPE *GetPropertyType)(IPropertyDescription2 *This, VARTYPE *type);
    HRESULT(STDMETHODCALLTYPE *GetDisplayName)(IPropertyDescription2 *This, LPWSTR *name);
    HRESULT(STDMETHODCALLTYPE *GetEditInvitation)(IPropertyDescription2 *This, LPWSTR *text);
    HRESULT(STDMETHODCALLTYPE *GetTypeFlags)(IPropertyDescription2 *This, PROPDESC_TYPE_FLAGS mask, PROPDESC_TYPE_FLAGS *flags);
    HRESULT(STDMETHODCALLTYPE *GetViewFlags)(IPropertyDescription2 *This, PROPDESC_VIEW_FLAGS *flags);
    HRESULT(STDMETHODCALLTYPE *GetDefaultColumnWidth)(IPropertyDescription2 *This, UINT *width);
    HRESULT(STDMETHODCALLTYPE *GetDisplayType)(IPropertyDescription2 *This, PROPDESC_DISPLAYTYPE *type);
    HRESULT(STDMETHODCALLTYPE *GetColumnState)(IPropertyDescription2 *This, SHCOLSTATEF *state);
    HRESULT(STDMETHODCALLTYPE *GetGroupingRange)(IPropertyDescription2 *This, PROPDESC_GROUPING_RANGE *range);
    HRESULT(STDMETHODCALLTYPE *GetRelativeDescriptionType)(IPropertyDescription2 *This, PROPDESC_RELATIVEDESCRIPTION_TYPE *type);
    HRESULT(STDMETHODCALLTYPE *GetRelativeDescription)(IPropertyDescription2 *This, REFPROPVARIANT first, REFPROPVARIANT second, LPWSTR *a, LPWSTR *b);
    HRESULT(STDMETHODCALLTYPE *GetSortDescription)(IPropertyDescription2 *This, PROPDESC_SORTDESCRIPTION *sort);
    HRESULT(STDMETHODCALLTYPE *GetSortDescriptionLabel)(IPropertyDescription2 *This, BOOL descending, LPWSTR *label);
    HRESULT(STDMETHODCALLTYPE *GetAggregationType)(IPropertyDescription2 *This, PROPDESC_AGGREGATION_TYPE *type);
    HRESULT(STDMETHODCALLTYPE *GetConditionType)(IPropertyDescription2 *This, PROPDESC_CONDITION_TYPE *type, CONDITION_OPERATION *operation);
    HRESULT(STDMETHODCALLTYPE *GetEnumTypeList)(IPropertyDescription2 *This, REFIID riid, void **object);
    HRESULT(STDMETHODCALLTYPE *CoerceToCanonicalValue)(IPropertyDescription2 *This, PROPVARIANT *value);
    HRESULT(STDMETHODCALLTYPE *FormatForDisplay)(IPropertyDescription2 *This, REFPROPVARIANT value, PROPDESC_FORMAT_FLAGS flags, LPWSTR *text);
    HRESULT(STDMETHODCALLTYPE *IsValueCanonical)(IPropertyDescription2 *This, REFPROPVARIANT value);
    HRESULT(STDMETHODCALLTYPE *GetImageReferenceForValue)(IPropertyDescription2 *This, REFPROPVARIANT value, LPWSTR *image);
} IPropertyDescription2Vtbl;
struct IPropertyDescription2 {
    const IPropertyDescription2Vtbl *lpVtbl;
};
MIME_DEFINE_GUID(IID_IPropertyDescription2, 0x57D2EDED, 0x5062, 0x400E, 0xB1, 0x07, 0x5D, 0xAE, 0x79, 0xFE, 0x57, 0xA6);

typedef struct IPropertyDescriptionAliasInfo IPropertyDescriptionAliasInfo;
typedef struct IPropertyDescriptionAliasInfoVtbl {
    HRESULT(STDMETHODCALLTYPE *QueryInterface)(IPropertyDescriptionAliasInfo *This, REFIID riid, void **object);
    ULONG(STDMETHODCALLTYPE *AddRef)(IPropertyDescriptionAliasInfo *This);
    ULONG(STDMETHODCALLTYPE *Release)(IPropertyDescriptionAliasInfo *This);
    HRESULT(STDMETHODCALLTYPE *GetSortByAlias)(IPropertyDescriptionAliasInfo *This, REFIID riid, void **object);
    HRESULT(STDMETHODCALLTYPE *GetAdditionalSortByAliases)(IPropertyDescriptionAliasInfo *This, REFIID riid, void **object);
} IPropertyDescriptionAliasInfoVtbl;
struct IPropertyDescriptionAliasInfo {
    const IPropertyDescriptionAliasInfoVtbl *lpVtbl;
};
MIME_DEFINE_GUID(IID_IPropertyDescriptionAliasInfo, 0xF67104FC, 0x2AF9, 0x46FD, 0xB3, 0x2D, 0x24, 0x3C, 0x14, 0x04, 0xF3, 0xD1);

typedef struct IPropertyDescriptionSearchInfo IPropertyDescriptionSearchInfo;
typedef struct IPropertyDescriptionSearchInfoVtbl {
    HRESULT(STDMETHODCALLTYPE *QueryInterface)(IPropertyDescriptionSearchInfo *This, REFIID riid, void **object);
    ULONG(STDMETHODCALLTYPE *AddRef)(IPropertyDescriptionSearchInfo *This);
    ULONG(STDMETHODCALLTYPE *Release)(IPropertyDescriptionSearchInfo *This);
    HRESULT(STDMETHODCALLTYPE *GetSearchInfoFlags)(IPropertyDescriptionSearchInfo *This, PROPDESC_SEARCHINFO_FLAGS *flags);
    HRESULT(STDMETHODCALLTYPE *GetColumnIndexType)(IPropertyDescriptionSearchInfo *This, PROPDESC_COLUMNINDEX_TYPE *type);
    HRESULT(STDMETHODCALLTYPE *GetProjectionString)(IPropertyDescriptionSearchInfo *This, LPWSTR *text);
    HRESULT(STDMETHODCALLTYPE *GetMaxSize)(IPropertyDescriptionSearchInfo *This, UINT *size);
} IPropertyDescriptionSearchInfoVtbl;
struct IPropertyDescriptionSearchInfo {
    const IPropertyDescriptionSearchInfoVtbl *lpVtbl;
};
MIME_DEFINE_GUID(IID_IPropertyDescriptionSearchInfo, 0x078F91BD, 0x29A2, 0x440F, 0x92, 0x4E, 0x46, 0xA2, 0x91, 0x52, 0x45, 0x20);

typedef struct IPropertyDescriptionRelatedPropertyInfo IPropertyDescriptionRelatedPropertyInfo;
typedef struct IPropertyDescriptionRelatedPropertyInfoVtbl {
    HRESULT(STDMETHODCALLTYPE *QueryInterface)(IPropertyDescriptionRelatedPropertyInfo *This, REFIID riid, void **object);
    ULONG(STDMETHODCALLTYPE *AddRef)(IPropertyDescriptionRelatedPropertyInfo *This);
    ULONG(STDMETHODCALLTYPE *Release)(IPropertyDescriptionRelatedPropertyInfo *This);
    HRESULT(STDMETHODCALLTYPE *GetRelatedProperty)(IPropertyDescriptionRelatedPropertyInfo *This, LPCWSTR relationship, REFIID riid, void **object);
} IPropertyDescriptionRelatedPropertyInfoVtbl;
struct IPropertyDescriptionRelatedPropertyInfo {
    const IPropertyDescriptionRelatedPropertyInfoVtbl *lpVtbl;
};
MIME_DEFINE_GUID(IID_IPropertyDescriptionRelatedPropertyInfo, 0x507393F4, 0x2A3D, 0x4A60, 0xB5, 0x9E, 0xD9, 0xC7, 0x57, 0x16, 0xC2, 0xDD);

typedef struct IPropertyEnumType IPropertyEnumType;
typedef struct IPropertyEnumTypeVtbl {
    HRESULT(STDMETHODCALLTYPE *QueryInterface)(IPropertyEnumType *This, REFIID riid, void **object);
    ULONG(STDMETHODCALLTYPE *AddRef)(IPropertyEnumType *This);
    ULONG(STDMETHODCALLTYPE *Release)(IPropertyEnumType *This);
    HRESULT(STDMETHODCALLTYPE *GetEnumType)(IPropertyEnumType *This, PROPENUMTYPE *type);
    HRESULT(STDMETHODCALLTYPE *GetValue)(IPropertyEnumType *This, PROPVARIANT *value);
    HRESULT(STDMETHODCALLTYPE *GetRangeMinValue)(IPropertyEnumType *This, PROPVARIANT *value);
    HRESULT(STDMETHODCALLTYPE *GetRangeSetValue)(IPropertyEnumType *This, PROPVARIANT *value);
    HRESULT(STDMETHODCALLTYPE *GetDisplayText)(IPropertyEnumType *This, LPWSTR *text);
} IPropertyEnumTypeVtbl;
struct IPropertyEnumType {
    const IPropertyEnumTypeVtbl *lpVtbl;
};
MIME_DEFINE_GUID(IID_IPropertyEnumType, 0x11E1FBF9, 0x2D56, 0x4A6B, 0x8D, 0xB3, 0x7C, 0xD1, 0x93, 0xA4, 0x71, 0xF2);

typedef struct IPropertyEnumType2 IPropertyEnumType2;
typedef struct IPropertyEnumType2Vtbl {
    HRESULT(STDMETHODCALLTYPE *QueryInterface)(IPropertyEnumType2 *This, REFIID riid, void **object);
    ULONG(STDMETHODCALLTYPE *AddRef)(IPropertyEnumType2 *This);
    ULONG(STDMETHODCALLTYPE *Release)(IPropertyEnumType2 *This);
    HRESULT(STDMETHODCALLTYPE *GetEnumType)(IPropertyEnumType2 *This, PROPENUMTYPE *type);
    HRESULT(STDMETHODCALLTYPE *GetValue)(IPropertyEnumType2 *This, PROPVARIANT *value);
    HRESULT(STDMETHODCALLTYPE *GetRangeMinValue)(IPropertyEnumType2 *This, PROPVARIANT *value);
    HRESULT(STDMETHODCALLTYPE *GetRangeSetValue)(IPropertyEnumType2 *This, PROPVARIANT *value);
    HRESULT(STDMETHODCALLTYPE *GetDisplayText)(IPropertyEnumType2 *This, LPWSTR *text);
    HRESULT(STDMETHODCALLTYPE *GetImageReference)(IPropertyEnumType2 *This, LPWSTR *image);
} IPropertyEnumType2Vtbl;
struct IPropertyEnumType2 {
    const IPropertyEnumType2Vtbl *lpVtbl;
};
MIME_DEFINE_GUID(IID_IPropertyEnumType2, 0x9B6E051C, 0x5DDD, 0x4321, 0x90, 0x70, 0xFE, 0x2A, 0xCB, 0x55, 0xE7, 0x94);

typedef struct IPropertyEnumTypeList IPropertyEnumTypeList;
typedef struct IPropertyEnumTypeListVtbl {
    HRESULT(STDMETHODCALLTYPE *QueryInterface)(IPropertyEnumTypeList *This, REFIID riid, void **object);
    ULONG(STDMETHODCALLTYPE *AddRef)(IPropertyEnumTypeList *This);
    ULONG(STDMETHODCALLTYPE *Release)(IPropertyEnumTypeList *This);
    HRESULT(STDMETHODCALLTYPE *GetCount)(IPropertyEnumTypeList *This, UINT *count);
    HRESULT(STDMETHODCALLTYPE *GetAt)(IPropertyEnumTypeList *This, UINT index, REFIID riid, void **object);
    HRESULT(STDMETHODCALLTYPE *GetConditionAt)(IPropertyEnumTypeList *This, UINT index, REFIID riid, void **object);
    HRESULT(STDMETHODCALLTYPE *FindMatchingIndex)(IPropertyEnumTypeList *This, REFPROPVARIANT value, UINT *index);
} IPropertyEnumTypeListVtbl;
struct IPropertyEnumTypeList {
    const IPropertyEnumTypeListVtbl *lpVtbl;
};
MIME_DEFINE_GUID(IID_IPropertyEnumTypeList, 0xA99400F4, 0x3D84, 0x4557, 0x94, 0xBA, 0x12, 0x42, 0xFB, 0x2C, 0xC9, 0xA6);

typedef struct IPropertyDescriptionList IPropertyDescriptionList;
typedef struct IPropertyDescriptionListVtbl {
    HRESULT(STDMETHODCALLTYPE *QueryInterface)(IPropertyDescriptionList *This, REFIID riid, void **object);
    ULONG(STDMETHODCALLTYPE *AddRef)(IPropertyDescriptionList *This);
    ULONG(STDMETHODCALLTYPE *Release)(IPropertyDescriptionList *This);
    HRESULT(STDMETHODCALLTYPE *GetCount)(IPropertyDescriptionList *This, UINT *count);
    HRESULT(STDMETHODCALLTYPE *GetAt)(IPropertyDescriptionList *This, UINT index, REFIID riid, void **object);
} IPropertyDescriptionListVtbl;
struct IPropertyDescriptionList {
    const IPropertyDescriptionListVtbl *lpVtbl;
};
MIME_DEFINE_GUID(IID_IPropertyDescriptionList, 0x1F9FC1D0, 0xC39B, 0x4B26, 0x81, 0x7F, 0x01, 0x19, 0x67, 0xD3, 0x44, 0x0E);

typedef struct IPropertySystem IPropertySystem;
typedef struct IPropertySystemVtbl {
    HRESULT(STDMETHODCALLTYPE *QueryInterface)(IPropertySystem *This, REFIID riid, void **object);
    ULONG(STDMETHODCALLTYPE *AddRef)(IPropertySystem *This);
    ULONG(STDMETHODCALLTYPE *Release)(IPropertySystem *This);
    HRESULT(STDMETHODCALLTYPE *GetPropertyDescription)(IPropertySystem *This, REFPROPERTYKEY key, REFIID riid, void **object);
    HRESULT(STDMETHODCALLTYPE *GetPropertyDescriptionByName)(IPropertySystem *This, LPCWSTR name, REFIID riid, void **object);
    HRESULT(STDMETHODCALLTYPE *GetPropertyDescriptionListFromString)(IPropertySystem *This, LPCWSTR list, REFIID riid, void **object);
    HRESULT(STDMETHODCALLTYPE *EnumeratePropertyDescriptions)(IPropertySystem *This, PROPDESC_ENUMFILTER filter, REFIID riid, void **object);
    HRESULT(STDMETHODCALLTYPE *FormatForDisplay)(IPropertySystem *This, REFPROPERTYKEY key, REFPROPVARIANT value, PROPDESC_FORMAT_FLAGS flags, LPWSTR text, DWORD length);
    HRESULT(STDMETHODCALLTYPE *FormatForDisplayAlloc)(IPropertySystem *This, REFPROPERTYKEY key, REFPROPVARIANT value, PROPDESC_FORMAT_FLAGS flags, LPWSTR *text);
    HRESULT(STDMETHODCALLTYPE *RegisterPropertySchema)(IPropertySystem *This, LPCWSTR path);
    HRESULT(STDMETHODCALLTYPE *UnregisterPropertySchema)(IPropertySystem *This, LPCWSTR path);
    HRESULT(STDMETHODCALLTYPE *RefreshPropertySchema)(IPropertySystem *This);
} IPropertySystemVtbl;
struct IPropertySystem {
    const IPropertySystemVtbl *lpVtbl;
};
MIME_DEFINE_GUID(IID_IPropertySystem, 0xCA724E8A, 0xC3E6, 0x442B, 0x88, 0xA4, 0x6F, 0xB0, 0xDB, 0x80, 0x35, 0xA3);

typedef struct IObjectWithPropertyKey IObjectWithPropertyKey;
typedef struct IObjectWithPropertyKeyVtbl {
    HRESULT(STDMETHODCALLTYPE *QueryInterface)(IObjectWithPropertyKey *This, REFIID riid, void **object);
    ULONG(STDMETHODCALLTYPE *AddRef)(IObjectWithPropertyKey *This);
    ULONG(STDMETHODCALLTYPE *Release)(IObjectWithPropertyKey *This);
    HRESULT(STDMETHODCALLTYPE *SetPropertyKey)(IObjectWithPropertyKey *This, REFPROPERTYKEY key);
    HRESULT(STDMETHODCALLTYPE *GetPropertyKey)(IObjectWithPropertyKey *This, PROPERTYKEY *key);
} IObjectWithPropertyKeyVtbl;
struct IObjectWithPropertyKey {
    const IObjectWithPropertyKeyVtbl *lpVtbl;
};
MIME_DEFINE_GUID(IID_IObjectWithPropertyKey, 0xFC0CA0A7, 0xC316, 0x4FD2, 0x90, 0x31, 0x3E, 0x62, 0x8E, 0x6D, 0x4F, 0x23);

typedef struct IPropertyChange IPropertyChange;
typedef struct IPropertyChangeVtbl {
    HRESULT(STDMETHODCALLTYPE *QueryInterface)(IPropertyChange *This, REFIID riid, void **object);
    ULONG(STDMETHODCALLTYPE *AddRef)(IPropertyChange *This);
    ULONG(STDMETHODCALLTYPE *Release)(IPropertyChange *This);
    HRESULT(STDMETHODCALLTYPE *SetPropertyKey)(IPropertyChange *This, REFPROPERTYKEY key);
    HRESULT(STDMETHODCALLTYPE *GetPropertyKey)(IPropertyChange *This, PROPERTYKEY *key);
    HRESULT(STDMETHODCALLTYPE *ApplyToPropVariant)(IPropertyChange *This, REFPROPVARIANT source, PROPVARIANT *dest);
} IPropertyChangeVtbl;
struct IPropertyChange {
    const IPropertyChangeVtbl *lpVtbl;
};
MIME_DEFINE_GUID(IID_IPropertyChange, 0xF917BC8A, 0x1BBA, 0x4478, 0xA2, 0x45, 0x1B, 0xDE, 0x03, 0xEB, 0x94, 0x31);

typedef struct IPropertyChangeArray IPropertyChangeArray;
typedef struct IPropertyChangeArrayVtbl {
    HRESULT(STDMETHODCALLTYPE *QueryInterface)(IPropertyChangeArray *This, REFIID riid, void **object);
    ULONG(STDMETHODCALLTYPE *AddRef)(IPropertyChangeArray *This);
    ULONG(STDMETHODCALLTYPE *Release)(IPropertyChangeArray *This);
    HRESULT(STDMETHODCALLTYPE *GetCount)(IPropertyChangeArray *This, UINT *count);
    HRESULT(STDMETHODCALLTYPE *GetAt)(IPropertyChangeArray *This, UINT index, REFIID riid, void **object);
    HRESULT(STDMETHODCALLTYPE *InsertAt)(IPropertyChangeArray *This, UINT index, IPropertyChange *change);
    HRESULT(STDMETHODCALLTYPE *Append)(IPropertyChangeArray *This, IPropertyChange *change);
    HRESULT(STDMETHODCALLTYPE *AppendOrReplace)(IPropertyChangeArray *This, IPropertyChange *change);
    HRESULT(STDMETHODCALLTYPE *RemoveAt)(IPropertyChangeArray *This, UINT index);
    HRESULT(STDMETHODCALLTYPE *IsKeyInArray)(IPropertyChangeArray *This, REFPROPERTYKEY key);
} IPropertyChangeArrayVtbl;
struct IPropertyChangeArray {
    const IPropertyChangeArrayVtbl *lpVtbl;
};
MIME_DEFINE_GUID(IID_IPropertyChangeArray, 0x380F5CAD, 0x1B5E, 0x42F2, 0x80, 0x5D, 0x63, 0x7F, 0xD3, 0x92, 0xD3, 0x1E);

typedef struct IInitializeWithStream IInitializeWithStream;
typedef struct IInitializeWithStreamVtbl {
    HRESULT(STDMETHODCALLTYPE *QueryInterface)(IInitializeWithStream *This, REFIID riid, void **object);
    ULONG(STDMETHODCALLTYPE *AddRef)(IInitializeWithStream *This);
    ULONG(STDMETHODCALLTYPE *Release)(IInitializeWithStream *This);
    HRESULT(STDMETHODCALLTYPE *Initialize)(IInitializeWithStream *This, IStream *stream, DWORD mode);
} IInitializeWithStreamVtbl;
struct IInitializeWithStream {
    const IInitializeWithStreamVtbl *lpVtbl;
};
MIME_DEFINE_GUID(IID_IInitializeWithStream, 0xB824B49D, 0x22AC, 0x4161, 0xAC, 0x8A, 0x99, 0x16, 0xE8, 0xFA, 0x3F, 0x7F);

typedef struct IInitializeWithFile IInitializeWithFile;
typedef struct IInitializeWithFileVtbl {
    HRESULT(STDMETHODCALLTYPE *QueryInterface)(IInitializeWithFile *This, REFIID riid, void **object);
    ULONG(STDMETHODCALLTYPE *AddRef)(IInitializeWithFile *This);
    ULONG(STDMETHODCALLTYPE *Release)(IInitializeWithFile *This);
    HRESULT(STDMETHODCALLTYPE *Initialize)(IInitializeWithFile *This, LPCWSTR path, DWORD mode);
} IInitializeWithFileVtbl;
struct IInitializeWithFile {
    const IInitializeWithFileVtbl *lpVtbl;
};
MIME_DEFINE_GUID(IID_IInitializeWithFile, 0xB7D14566, 0x0509, 0x4CCE, 0xA7, 0x1F, 0x0A, 0x55, 0x42, 0x33, 0xBD, 0x9B);

typedef struct IInitializeWithItem IInitializeWithItem;
typedef struct IInitializeWithItemVtbl {
    HRESULT(STDMETHODCALLTYPE *QueryInterface)(IInitializeWithItem *This, REFIID riid, void **object);
    ULONG(STDMETHODCALLTYPE *AddRef)(IInitializeWithItem *This);
    ULONG(STDMETHODCALLTYPE *Release)(IInitializeWithItem *This);
    HRESULT(STDMETHODCALLTYPE *Initialize)(IInitializeWithItem *This, IShellItem *item, DWORD mode);
} IInitializeWithItemVtbl;
struct IInitializeWithItem {
    const IInitializeWithItemVtbl *lpVtbl;
};
MIME_DEFINE_GUID(IID_IInitializeWithItem, 0x7F73BE3F, 0xFB79, 0x493C, 0xA6, 0xC7, 0x7E, 0xE1, 0x4E, 0x24, 0x58, 0x41);

typedef struct IPropertyStoreFactory IPropertyStoreFactory;
typedef struct IPropertyStoreFactoryVtbl {
    HRESULT(STDMETHODCALLTYPE *QueryInterface)(IPropertyStoreFactory *This, REFIID riid, void **object);
    ULONG(STDMETHODCALLTYPE *AddRef)(IPropertyStoreFactory *This);
    ULONG(STDMETHODCALLTYPE *Release)(IPropertyStoreFactory *This);
    HRESULT(STDMETHODCALLTYPE *GetPropertyStore)(IPropertyStoreFactory *This, GETPROPERTYSTOREFLAGS flags, IUnknown *unknown, REFIID riid, void **object);
    HRESULT(STDMETHODCALLTYPE *GetPropertyStoreForKeys)(IPropertyStoreFactory *This, const PROPERTYKEY *keys, UINT count, GETPROPERTYSTOREFLAGS flags, REFIID riid, void **object);
} IPropertyStoreFactoryVtbl;
struct IPropertyStoreFactory {
    const IPropertyStoreFactoryVtbl *lpVtbl;
};
MIME_DEFINE_GUID(IID_IPropertyStoreFactory, 0xBC110B6D, 0x57E8, 0x4148, 0xA9, 0xC6, 0x91, 0x01, 0x5A, 0xB2, 0xF3, 0xA5);

typedef struct IDelayedPropertyStoreFactory IDelayedPropertyStoreFactory;
typedef struct IDelayedPropertyStoreFactoryVtbl {
    HRESULT(STDMETHODCALLTYPE *QueryInterface)(IDelayedPropertyStoreFactory *This, REFIID riid, void **object);
    ULONG(STDMETHODCALLTYPE *AddRef)(IDelayedPropertyStoreFactory *This);
    ULONG(STDMETHODCALLTYPE *Release)(IDelayedPropertyStoreFactory *This);
    HRESULT(STDMETHODCALLTYPE *GetPropertyStore)(IDelayedPropertyStoreFactory *This, GETPROPERTYSTOREFLAGS flags, IUnknown *unknown, REFIID riid, void **object);
    HRESULT(STDMETHODCALLTYPE *GetPropertyStoreForKeys)(IDelayedPropertyStoreFactory *This, const PROPERTYKEY *keys, UINT count, GETPROPERTYSTOREFLAGS flags, REFIID riid, void **object);
    HRESULT(STDMETHODCALLTYPE *GetDelayedPropertyStore)(IDelayedPropertyStoreFactory *This, GETPROPERTYSTOREFLAGS flags, DWORD id, REFIID riid, void **object);
} IDelayedPropertyStoreFactoryVtbl;
struct IDelayedPropertyStoreFactory {
    const IDelayedPropertyStoreFactoryVtbl *lpVtbl;
};
MIME_DEFINE_GUID(IID_IDelayedPropertyStoreFactory, 0x40D4577F, 0xE237, 0x4BDB, 0xBD, 0x69, 0x58, 0xF0, 0x89, 0x43, 0x1B, 0x6A);

MIME_DEFINE_GUID(CLSID_PropertySystem, 0xB8967F85, 0x58AE, 0x4F46, 0x9F, 0xB2, 0x5D, 0x79, 0x04, 0x79, 0x8F, 0x4B);
MIME_DEFINE_GUID(CLSID_InMemoryPropertyStore, 0x9A02E012, 0x6303, 0x4E1E, 0xB9, 0xA1, 0x63, 0x0F, 0x80, 0x25, 0x92, 0xC5);
MIME_DEFINE_GUID(CLSID_InMemoryPropertyStoreMarshalByValue, 0xD4CA0E2D, 0x6DA7, 0x4B75, 0xA9, 0x7C, 0x5F, 0x30, 0x6F, 0x0E, 0xAE, 0xDC);
MIME_DEFINE_GUID(CLSID_PropertyStoreOverFolder, 0x0340F119, 0xA598, 0x4ED9, 0xB0, 0xAC, 0x6F, 0x6A, 0x12, 0xD3, 0xE7, 0x55);

PSSTDAPI PSGetPropertySystem(REFIID riid, void **object);
PSSTDAPI PSGetPropertyDescription(REFPROPERTYKEY key, REFIID riid, void **object);
PSSTDAPI PSGetPropertyDescriptionByName(LPCWSTR name, REFIID riid, void **object);
PSSTDAPI PSGetPropertyDescriptionListFromString(LPCWSTR list, REFIID riid, void **object);
PSSTDAPI PSEnumeratePropertyDescriptions(PROPDESC_ENUMFILTER filter, REFIID riid, void **object);
PSSTDAPI PSRegisterPropertySchema(LPCWSTR path);
PSSTDAPI PSUnregisterPropertySchema(LPCWSTR path);
PSSTDAPI PSRefreshPropertySchema(void);
PSSTDAPI PSFormatForDisplay(REFPROPERTYKEY key, REFPROPVARIANT value, PROPDESC_FORMAT_FLAGS flags, LPWSTR text,
                            DWORD length);
PSSTDAPI PSFormatForDisplayAlloc(REFPROPERTYKEY key, REFPROPVARIANT value, PROPDESC_FORMAT_FLAGS flags, LPWSTR *text);
PSSTDAPI PSFormatPropertyValue(IPropertyStore *store, IPropertyDescription *description, PROPDESC_FORMAT_FLAGS flags,
                               LPWSTR *text);
PSSTDAPI PSGetImageReferenceForValue(REFPROPERTYKEY key, REFPROPVARIANT value, LPWSTR *image);
PSSTDAPI PSCoerceToCanonicalValue(REFPROPERTYKEY key, PROPVARIANT *value);
PSSTDAPI PSGetPropertyValue(IPropertyStore *store, IPropertyDescription *description, PROPVARIANT *value);
PSSTDAPI PSSetPropertyValue(IPropertyStore *store, IPropertyDescription *description, REFPROPVARIANT value);
PSSTDAPI PSCreateSimplePropertyChange(PKA_FLAGS action, REFPROPERTYKEY key, REFPROPVARIANT value, REFIID riid,
                                      void **object);
PSSTDAPI PSCreatePropertyChangeArray(const PROPERTYKEY *keys, const PKA_FLAGS *actions, const PROPVARIANT *values,
                                     UINT count, REFIID riid, void **object);
PSSTDAPI PSCreateMultiplexPropertyStore(IUnknown **stores, DWORD count, REFIID riid, void **object);
PSSTDAPI PSCreateDelayedMultiplexPropertyStore(GETPROPERTYSTOREFLAGS flags, IDelayedPropertyStoreFactory *factory,
                                               const DWORD *ids, DWORD count, REFIID riid, void **object);
PSSTDAPI PSCreateAdapterFromPropertyStore(IPropertyStore *store, REFIID riid, void **object);
PSSTDAPI PSCreatePropertyStoreFromObject(IUnknown *object, DWORD mode, REFIID riid, void **result);
PSSTDAPI PSCreatePropertyStoreFromPropertySetStorage(IPropertySetStorage *storage, DWORD mode, REFIID riid,
                                                     void **result);
PSSTDAPI PSGetItemPropertyHandler(IUnknown *item, BOOL readwrite, REFIID riid, void **object);
PSSTDAPI PSGetItemPropertyHandlerWithCreateObject(IUnknown *item, BOOL readwrite, IUnknown *create, REFIID riid,
                                                  void **object);
PSSTDAPI PSLookupPropertyHandlerCLSID(LPCWSTR path, CLSID *clsid);

#endif
