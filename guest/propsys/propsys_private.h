#pragma once

#define PSSTDAPI HRESULT WINAPI
#define PROPSYSAPI
#include <mime/win32/propsys.h>
#include <mime/win32/oleauto.h>
#include <mime/win32/winnls.h>
#include <mime/win32/winuser.h>
#include <mime/win32/shlwapi.h>

/* 1 for a scalar, else the element count of a vector or one dimensional array. */
ULONG PsElementCount(const PROPVARIANT *value);
BOOL PsValidType(VARTYPE vt);
BOOL PsValidValue(const PROPVARIANT *value);
/* 0 when base is not a vector element type. */
UINT PsElementSize(VARTYPE base);

HRESULT PsAllocString(LPCWSTR source, LPWSTR *out);
HRESULT PsAllocStringLen(LPCWSTR source, SIZE_T length, LPWSTR *out);
void *PsAllocZero(SIZE_T size);
HRESULT PsAnsiToWideAlloc(LPCSTR source, LPWSTR *out);
HRESULT PsWideToAnsiAlloc(LPCWSTR source, LPSTR *out);
HRESULT PsCopyString(LPWSTR buffer, UINT length, LPCWSTR source);

BOOL PsGuidFromString(LPCWSTR string, GUID *guid);
int PsStrCmpLogical(LPCWSTR a, LPCWSTR b);
int PsStrCmp(LPCWSTR a, LPCWSTR b, BOOL ignore_case);
HRESULT PsDoubleToString(double value, LPWSTR *out);
BOOL PsParseDouble(LPCWSTR string, double *value);

/* one element of a vector, array or by reference value, or the scalar itself. */
HRESULT PsElementPointer(const PROPVARIANT *value, ULONG index, const void **element);

/* ------------------------------------------------------------------ xml (xml.c) */

typedef struct XAttr XAttr;
typedef struct XElem XElem;
struct XAttr {
    LPWSTR name, value;
    XAttr *next;
};
struct XElem {
    LPWSTR name;
    XAttr *attrs;
    XElem *child, *next;
};
HRESULT XmlParse(const BYTE *data, SIZE_T size, XElem **root);
void XmlFree(XElem *element);
/* an attribute by name, case insensitive. */
LPCWSTR XmlAttr(const XElem *element, LPCWSTR name);
/* the element name without its namespace prefix, case insensitive. */
BOOL XmlIs(const XElem *element, LPCWSTR name);

/* ------------------------------------------------------------------ schema (schema.c, desc.c) */

/*
 * a parsed schema lives in one block of memory whose strings and arrays are reached by offsets from its start, so the
 * block is the cache file as it is. offset 0 is nothing.
 */
#define CACHE_MAGIC 0x434D5350u
#define CACHE_VERSION 19

typedef struct {
    DWORD magic, version, size;
    GUID id;
    FILETIME source_time;
    DWORD source_size;
    DWORD invalid;
    DWORD system;
    DWORD count, descs;
} CACHE_HEADER;

typedef struct {
    DWORD name, label, invitation, sort_alias, more_aliases;
    PROPERTYKEY key;
    DWORD vt, type_flags, view_flags, col_width, display_type, column_state;
    DWORD grouping, relative_type, sort_desc, aggregation, cond_type, cond_op;
    /* the formatAs of the display type and the parts a date time has. */
    DWORD format_as, date_as, time_as, duration_as, true_text, false_text;
    DWORD search_flags, col_index, max_size, projection;
    DWORD enum_default_text, enum_use_default, enum_count, enums;
    DWORD rel_count, rels;
} PDESC;

typedef struct {
    DWORD type, name, value, min, set, text, image;
} ENUMINFO;

typedef struct {
    DWORD relationship, property;
} RELPROP;

typedef struct SCHEMA SCHEMA;
struct SCHEMA {
    LONG refs;
    BYTE *base;
    const CACHE_HEADER *header;
    const PDESC *descs;
    DWORD *by_key, *by_name;
};

#define OFS_STR(schema, ofs) ((ofs) ? (LPCWSTR)((schema)->base + (ofs)) : nullptr)

/* the pieces of formatAs the description keeps. */
enum { FMT_DEFAULT = 0 };

HRESULT SchemaBuild(const BYTE *xml, SIZE_T size, const GUID *id, BOOL system, BYTE **image, DWORD *image_size);
SCHEMA *SchemaFromImage(BYTE *image);
void SchemaRelease(SCHEMA *schema);
void SchemaAddRef(SCHEMA *schema);

/* the lookups over every registered schema. */
HRESULT SchemaFindKey(REFPROPERTYKEY key, SCHEMA **schema, DWORD *index);
HRESULT SchemaFindName(LPCWSTR name, SCHEMA **schema, DWORD *index);
HRESULT SchemaEnumerate(PROPDESC_ENUMFILTER filter, SCHEMA ***schemas, DWORD **indices, DWORD *count);
HRESULT SchemaRegister(LPCWSTR path);
HRESULT SchemaUnregister(LPCWSTR path);
HRESULT SchemaRefresh(void);

HRESULT DescCreate(SCHEMA *schema, DWORD index, REFIID riid, void **object);
HRESULT DescCreateWithView(SCHEMA *schema, DWORD index, DWORD mask, DWORD value, DWORD width, REFIID riid,
                           void **object);
HRESULT DescCreateListWithView(SCHEMA **schemas, DWORD *indices, DWORD *view, DWORD count, REFIID riid,
                               void **object);
HRESULT DescCreateList(SCHEMA **schemas, DWORD *indices, DWORD count, REFIID riid, void **object);
HRESULT DescCreateDummy(LPCWSTR name, REFIID riid, void **object);
/* the answers of a description, over the stored record. */
HRESULT DescFormat(SCHEMA *schema, const PDESC *desc, const PROPVARIANT *value, PROPDESC_FORMAT_FLAGS flags,
                   LPWSTR *text);
HRESULT DescCoerce(SCHEMA *schema, const PDESC *desc, PROPVARIANT *value);
HRESULT DescImageReference(SCHEMA *schema, const PDESC *desc, const PROPVARIANT *value, LPWSTR *image);
/* a string that may be a resource reference like @shell32.dll,-1234. */
HRESULT PsLoadIndirect(LPCWSTR source, LPWSTR *out);

extern HINSTANCE g_module;

/* ------------------------------------------------------------------ property sets and the small interfaces stores use */

typedef struct tagPS_PROPSPEC {
    ULONG kind;
    union {
        ULONG propid;
        LPOLESTR name;
    };
} PS_PROPSPEC;
#define PS_PRSPEC_LPWSTR 0
#define PS_PRSPEC_PROPID 1

typedef struct tagPS_STATPROPSTG {
    LPOLESTR name;
    ULONG propid;
    VARTYPE vt;
} PS_STATPROPSTG;

typedef struct tagPS_STATPROPSETSTG {
    GUID fmtid, clsid;
    DWORD flags;
    FILETIME mtime, ctime, atime;
    DWORD os_version;
} PS_STATPROPSETSTG;

typedef struct IPsEnumProp IPsEnumProp;
typedef struct IPsEnumPropVtbl {
    HRESULT(STDMETHODCALLTYPE *QueryInterface)(IPsEnumProp *This, REFIID riid, void **object);
    ULONG(STDMETHODCALLTYPE *AddRef)(IPsEnumProp *This);
    ULONG(STDMETHODCALLTYPE *Release)(IPsEnumProp *This);
    HRESULT(STDMETHODCALLTYPE *Next)(IPsEnumProp *This, ULONG count, void *items, ULONG *fetched);
    HRESULT(STDMETHODCALLTYPE *Skip)(IPsEnumProp *This, ULONG count);
    HRESULT(STDMETHODCALLTYPE *Reset)(IPsEnumProp *This);
    HRESULT(STDMETHODCALLTYPE *Clone)(IPsEnumProp *This, IPsEnumProp **clone);
} IPsEnumPropVtbl;
struct IPsEnumProp {
    const IPsEnumPropVtbl *lpVtbl;
};

typedef struct IPsPropertyStorage IPsPropertyStorage;
typedef struct IPsPropertyStorageVtbl {
    HRESULT(STDMETHODCALLTYPE *QueryInterface)(IPsPropertyStorage *This, REFIID riid, void **object);
    ULONG(STDMETHODCALLTYPE *AddRef)(IPsPropertyStorage *This);
    ULONG(STDMETHODCALLTYPE *Release)(IPsPropertyStorage *This);
    HRESULT(STDMETHODCALLTYPE *ReadMultiple)(IPsPropertyStorage *This, ULONG count, const PS_PROPSPEC *specs,
                                             PROPVARIANT *values);
    HRESULT(STDMETHODCALLTYPE *WriteMultiple)(IPsPropertyStorage *This, ULONG count, const PS_PROPSPEC *specs,
                                              const PROPVARIANT *values, ULONG first_name_id);
    HRESULT(STDMETHODCALLTYPE *DeleteMultiple)(IPsPropertyStorage *This, ULONG count, const PS_PROPSPEC *specs);
    HRESULT(STDMETHODCALLTYPE *ReadPropertyNames)(IPsPropertyStorage *This, ULONG count, const ULONG *ids,
                                                  LPOLESTR *names);
    HRESULT(STDMETHODCALLTYPE *WritePropertyNames)(IPsPropertyStorage *This, ULONG count, const ULONG *ids,
                                                   const LPOLESTR *names);
    HRESULT(STDMETHODCALLTYPE *DeletePropertyNames)(IPsPropertyStorage *This, ULONG count, const ULONG *ids);
    HRESULT(STDMETHODCALLTYPE *Commit)(IPsPropertyStorage *This, DWORD flags);
    HRESULT(STDMETHODCALLTYPE *Revert)(IPsPropertyStorage *This);
    HRESULT(STDMETHODCALLTYPE *Enum)(IPsPropertyStorage *This, IPsEnumProp **enumerator);
    HRESULT(STDMETHODCALLTYPE *SetTimes)(IPsPropertyStorage *This, const FILETIME *c, const FILETIME *a,
                                         const FILETIME *m);
    HRESULT(STDMETHODCALLTYPE *SetClass)(IPsPropertyStorage *This, REFCLSID clsid);
    HRESULT(STDMETHODCALLTYPE *Stat)(IPsPropertyStorage *This, void *stat);
} IPsPropertyStorageVtbl;
struct IPsPropertyStorage {
    const IPsPropertyStorageVtbl *lpVtbl;
};

struct IPropertySetStorage {
    const struct IPropertySetStorageVtbl {
        HRESULT(STDMETHODCALLTYPE *QueryInterface)(IPropertySetStorage *This, REFIID riid, void **object);
        ULONG(STDMETHODCALLTYPE *AddRef)(IPropertySetStorage *This);
        ULONG(STDMETHODCALLTYPE *Release)(IPropertySetStorage *This);
        HRESULT(STDMETHODCALLTYPE *Create)(IPropertySetStorage *This, REFFMTID fmtid, const CLSID *clsid, DWORD flags,
                                           DWORD mode, IPsPropertyStorage **storage);
        HRESULT(STDMETHODCALLTYPE *Open)(IPropertySetStorage *This, REFFMTID fmtid, DWORD mode,
                                         IPsPropertyStorage **storage);
        HRESULT(STDMETHODCALLTYPE *Delete)(IPropertySetStorage *This, REFFMTID fmtid);
        HRESULT(STDMETHODCALLTYPE *Enum)(IPropertySetStorage *This, IPsEnumProp **enumerator);
    } *lpVtbl;
};

typedef struct IPsStoreCapabilities IPsStoreCapabilities;
typedef struct IPsStoreCapabilitiesVtbl {
    HRESULT(STDMETHODCALLTYPE *QueryInterface)(IPsStoreCapabilities *This, REFIID riid, void **object);
    ULONG(STDMETHODCALLTYPE *AddRef)(IPsStoreCapabilities *This);
    ULONG(STDMETHODCALLTYPE *Release)(IPsStoreCapabilities *This);
    HRESULT(STDMETHODCALLTYPE *IsPropertyWritable)(IPsStoreCapabilities *This, REFPROPERTYKEY key);
} IPsStoreCapabilitiesVtbl;
struct IPsStoreCapabilities {
    const IPsStoreCapabilitiesVtbl *lpVtbl;
};
MIME_DEFINE_GUID(IID_IPsStoreCapabilities, 0xc8e2d566, 0x186e, 0x4d49, 0xbf, 0x41, 0x69, 0x09, 0xea, 0xd5, 0x6a, 0xcc);
MIME_DEFINE_GUID(IID_IPsPropertySetStorage, 0x0000013a, 0x0000, 0x0000, 0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46);

/* the internal interface that adds one more store to a multiplexed store. */
typedef struct IPsMultiplexStores IPsMultiplexStores;
typedef struct IPsMultiplexStoresVtbl {
    HRESULT(STDMETHODCALLTYPE *QueryInterface)(IPsMultiplexStores *This, REFIID riid, void **object);
    ULONG(STDMETHODCALLTYPE *AddRef)(IPsMultiplexStores *This);
    ULONG(STDMETHODCALLTYPE *Release)(IPsMultiplexStores *This);
    HRESULT(STDMETHODCALLTYPE *AddStore)(IPsMultiplexStores *This, IUnknown *store);
} IPsMultiplexStoresVtbl;
struct IPsMultiplexStores {
    const IPsMultiplexStoresVtbl *lpVtbl;
};
MIME_DEFINE_GUID(IID_IPsMultiplexStores, 0x75121952, 0xe0d0, 0x43e5, 0x93, 0x80, 0x1d, 0x80, 0x48, 0x3a, 0xcf, 0x72);

/* stores.c, window.c and handler.c. */
HRESULT WindowStoreCreate(HWND window, REFIID riid, void **object);
