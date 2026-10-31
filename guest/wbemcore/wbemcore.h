/*
 * the internals of the management instrumentation engine: objects with their properties, qualifiers and methods,
 * namespaces with their classes and instances, and the pieces that read and write them (object paths, the mof
 * compiler, queries, providers).
 */
#ifndef WBEMCORE_H
#define WBEMCORE_H

#include <rtl.h>
#include <mime/win32/objidl.h>
#include <mime/win32/wbemcli.h>
#include <mime/win32/winbase.h>

/* ------------------------------------------------------------------ memory and strings */

void *wb_alloc(SIZE_T size);
void *wb_realloc(void *memory, SIZE_T size);
void wb_free(void *memory);
WCHAR *wb_wcsdup(const WCHAR *text);
WCHAR *wb_wcsndup(const WCHAR *text, SIZE_T length);
/* a string built from a printf style format, to be freed. */
WCHAR *wb_format(const WCHAR *format, ...);
HRESULT wb_no_interface(const char *who, REFIID riid, void **out);

#define WB_GROW(array, count, capacity)                                                                                \
    do {                                                                                                               \
        if ((count) >= (capacity)) {                                                                                   \
            (capacity) = (capacity) ? (capacity) * 2 : 8;                                                              \
            (array) = wb_realloc((array), (SIZE_T)(capacity) * sizeof(*(array)));                                      \
        }                                                                                                              \
    } while (0)

/* a growing string. */
typedef struct WBuf {
    WCHAR *text;
    SIZE_T length, capacity;
} WBuf;
void wb_buf_add(WBuf *buf, const WCHAR *text);
void wb_buf_addn(WBuf *buf, const WCHAR *text, SIZE_T length);
void wb_buf_addc(WBuf *buf, WCHAR c);
void wb_buf_addf(WBuf *buf, const WCHAR *format, ...);
/* the text, owned by the caller; the buffer is empty afterwards. */
WCHAR *wb_buf_take(WBuf *buf);

#define wb_eq(a, b) (_wcsicmp((a), (b)) == 0)

/* ------------------------------------------------------------------ qualifiers, properties, methods */

typedef struct WQual {
    WCHAR *name;
    VARIANT value;
    LONG flavor;
} WQual;

typedef struct WQuals {
    WQual *items;
    UINT count, capacity;
} WQuals;

typedef struct WObj WObj;

typedef struct WProp {
    WCHAR *name;
    CIMTYPE type;
    VARIANT value; /* VT_NULL when the property has no value. */
    WQuals quals;
    WCHAR *origin; /* the class that declared it. */
    BOOL local;    /* declared or overridden by this class. */
} WProp;

typedef struct WMeth {
    WCHAR *name;
    WObj *in, *out;
    WQuals quals;
    WCHAR *origin;
    BOOL local;
} WMeth;

struct WObj {
    BOOL is_class;
    BOOL builtin;  /* part of the schema of the engine, not saved. */
    WCHAR *name;   /* the class of an instance. */
    WCHAR *super;  /* null for a base class. */
    WCHAR **derivation;
    UINT derivation_count;
    WCHAR *server, *ns;
    WQuals quals;
    WProp *props;
    UINT prop_count, prop_capacity;
    WMeth *meths;
    UINT meth_count, meth_capacity;
};

/* quals */
void wq_init(WQuals *quals);
void wq_free(WQuals *quals);
void wq_copy(WQuals *dst, const WQuals *src);
WQual *wq_find(const WQuals *quals, const WCHAR *name);
/* sets, replacing a qualifier of that name. */
void wq_set(WQuals *quals, const WCHAR *name, const VARIANT *value, LONG flavor);
BOOL wq_remove(WQuals *quals, const WCHAR *name);
/* true when the boolean qualifier is present and true. */
BOOL wq_true(const WQuals *quals, const WCHAR *name);

/* variants and cim types */
void wv_init(VARIANT *v);
HRESULT wv_copy(VARIANT *dst, const VARIANT *src);
/* the variant a property of that type holds, converted from any variant, or an error. */
HRESULT wv_coerce(VARIANT *dst, const VARIANT *src, CIMTYPE type);
VARTYPE wv_type_of(CIMTYPE type);
const WCHAR *wv_cim_name(CIMTYPE type);
/* the cim type of a mof type name, or CIM_ILLEGAL. */
CIMTYPE wv_cim_from_name(const WCHAR *name);
/* the value as text, for key paths and conditions. */
WCHAR *wv_text(const VARIANT *v, CIMTYPE type);
BOOL wv_is_null(const VARIANT *v);
/* compares two values of one type; false when they are not comparable. */
BOOL wv_compare(const VARIANT *a, const VARIANT *b, CIMTYPE type, int *order);

/* objects */
WObj *wo_new(BOOL is_class, const WCHAR *name, const WCHAR *super);
WObj *wo_clone(const WObj *object);
void wo_free(WObj *object);
WProp *wo_prop(const WObj *object, const WCHAR *name);
WMeth *wo_meth(const WObj *object, const WCHAR *name);
WProp *wo_add_prop(WObj *object, const WCHAR *name, CIMTYPE type, const VARIANT *value, const WCHAR *origin);
WMeth *wo_add_meth(WObj *object, const WCHAR *name, WObj *in, WObj *out, const WCHAR *origin);
BOOL wo_remove_prop(WObj *object, const WCHAR *name);
BOOL wo_remove_meth(WObj *object, const WCHAR *name);
/* an instance of a class, with its properties at their defaults. */
WObj *wo_spawn(const WObj *class_object);
/* a class derived from another: the properties and methods inherited, the qualifiers that propagate. */
WObj *wo_derive(const WObj *parent, const WCHAR *name);
/* true when the object's class is the class or inherits from it. */
BOOL wo_inherits(const WObj *object, const WCHAR *ancestor);
/* the dynasty: the base class at the top of the derivation. */
const WCHAR *wo_dynasty(const WObj *object);
/* the system properties as the get call returns them. */
BOOL wo_system_prop(const WObj *object, const WCHAR *name, VARIANT *value, CIMTYPE *type);
UINT wo_system_count(void);
const WCHAR *wo_system_name(UINT index);
/* the key properties, in order; the count. */
UINT wo_keys(const WObj *object, WProp **keys, UINT limit);
/* Class.Key="value",Key2=3 for an instance, the class name alone for a class; null when a key has no value. */
WCHAR *wo_relpath(const WObj *object);
WCHAR *wo_path(const WObj *object);
/* the mof text of an object. */
WCHAR *wo_text(const WObj *object, LONG flags);
/* equality of the instances (properties) or of the whole objects. */
BOOL wo_equal(const WObj *a, const WObj *b, LONG flags);

/* ------------------------------------------------------------------ object paths */

typedef struct WKey {
    WCHAR *name; /* null for a key with no name. */
    VARIANT value;
    CIMTYPE type;
} WKey;

typedef struct WScope {
    WCHAR *class_name;
    WKey *keys;
    UINT key_count;
    BOOL singleton;
} WScope;

typedef struct WPath {
    WCHAR *server;
    WCHAR **ns;
    UINT ns_count;
    WCHAR *class_name; /* null when the path names only a namespace. */
    WKey *keys;
    UINT key_count;
    BOOL singleton;
    WScope *scopes;
    UINT scope_count;
    BOOL has_class;
    BOOL has_server_prefix; /* started with two slashes. */
    BOOL native;
    ULONG parse_flags;
    WCHAR *original;
} WPath;

void wp_init(WPath *path);
void wp_free(WPath *path);
HRESULT wp_parse(WPath *path, const WCHAR *text, ULONG mode);
/* the path as text; flags are the get text flags of IWbemPath. */
WCHAR *wp_text(const WPath *path, LONG flags);
WCHAR *wp_keys_text(const WKey *keys, UINT count, BOOL singleton, BOOL quoted_names);
ULONGLONG wp_info(const WPath *path);
HRESULT wp_copy(WPath *dst, const WPath *src);

/* ------------------------------------------------------------------ namespaces and the repository */

typedef struct WNs WNs;

struct WNs {
    WCHAR *path; /* root\cimv2 */
    WObj **classes;
    UINT class_count, class_capacity;
    WObj **instances;
    UINT instance_count, instance_capacity;
    WCHAR *sddl;
};

/* locks the repository for the calling thread; refreshes it from disk when another process changed it. */
void wr_lock(void);
void wr_unlock(void);
/* the namespace of that path, creating it with the instance of __Namespace in its parent already there. */
WNs *wr_find_ns(const WCHAR *path);
WNs *wr_create_ns(const WCHAR *path);
BOOL wr_delete_ns(const WCHAR *path);
WObj *wr_find_class(WNs *ns, const WCHAR *name);
/* the class and every subclass; the classes are borrowed. */
UINT wr_subclasses(WNs *ns, const WCHAR *name, BOOL deep, WObj ***out);
HRESULT wr_put_class(WNs *ns, const WObj *class_object, LONG flags);
HRESULT wr_delete_class(WNs *ns, const WCHAR *name);
WObj *wr_find_instance(WNs *ns, const WCHAR *relpath);
HRESULT wr_put_instance(WNs *ns, const WObj *instance, LONG flags);
HRESULT wr_delete_instance(WNs *ns, const WCHAR *relpath);
/* writes the namespaces to the repository file. */
void wr_save(void);
/* compiles the mof text into the repository; the text is wide. */
typedef struct WMofResult {
    HRESULT result;
    LONG first_line, last_line, object_number;
    LONG phase;
} WMofResult;
HRESULT wmof_compile(const WCHAR *text, SIZE_T length, const WCHAR *default_ns, LONG options, LONG class_flags,
                     LONG instance_flags, WMofResult *info, BOOL builtin);
/* the schema of the engine, as mof. */
extern const WCHAR g_base_mof[];

/* ------------------------------------------------------------------ queries and providers */

typedef BOOL (*WEmit)(void *context, WObj *object);

HRESULT wq_execute(WNs *ns, const WCHAR *query, LONG flags, WEmit emit, void *context);
/* instances of a class, from its provider or from the repository, with every subclass when deep. */
HRESULT wprov_enum(WNs *ns, const WCHAR *class_name, BOOL deep, WEmit emit, void *context);
HRESULT wprov_get(WNs *ns, const WCHAR *relpath, WObj **object);
HRESULT wprov_put(WNs *ns, WObj *instance, LONG flags);
HRESULT wprov_delete(WNs *ns, const WCHAR *relpath);
HRESULT wprov_exec(WNs *ns, const WCHAR *object, const WCHAR *method, WObj *in, WObj **out);

/* ------------------------------------------------------------------ com objects */

HRESULT wc_object(WObj *object, REFIID riid, void **out); /* takes ownership of the object. */
WObj *wc_unwrap(IWbemClassObject *object);                /* borrowed, null when it is not ours. */
/* the blanket a client sets on an interface, which calls inside the process do not use but programs read back. */
typedef struct WSecurity {
    const IClientSecurityVtbl *lpVtbl;
    IUnknown *owner;
    DWORD authn_service, authz_service, authn_level, imp_level, capabilities;
} WSecurity;
void wc_security_init(WSecurity *security, IUnknown *owner);
/* the answer to a query for IClientSecurity, which is S_OK with the interface when the object has a blanket. */
HRESULT wc_security_query(WSecurity *security, REFIID riid, void **out);
HRESULT wc_context_new(IWbemContext **context);
HRESULT wc_callresult_new(HRESULT status, IWbemClassObject *object, BSTR string, IWbemServices *services,
                          IWbemCallResult **out);
typedef struct WList {
    WObj **items;
    UINT count, capacity;
} WList;
void wl_free(WList *list);
BOOL wl_emit(void *context, WObj *object); /* appends a clone. */
HRESULT wc_enum_new(WList *list, IEnumWbemClassObject **out); /* takes the items. */
HRESULT wc_services_new(const WCHAR *ns, IWbemContext *context, IWbemServices **out);
HRESULT wm_connect(const WCHAR *resource, const WCHAR *user, const WCHAR *password, LONG flags, IWbemContext *context,
                   IWbemServices **services);
void wr_init(void);
void wr_write_begin(void);
void wr_write_end(void);

/* ------------------------------------------------------------------ the rest */

HRESULT wm_class_factory(REFCLSID clsid, REFIID riid, void **out);
HRESULT wp_com_path_new(REFIID riid, void **out);
HRESULT wm_status_text_new(REFIID riid, void **out);
HRESULT wm_compiler_new(REFIID riid, void **out);
HRESULT wm_locator_new(REFIID riid, void **out);
HRESULT wm_login_new(REFIID riid, void **out);
extern LONG g_wbem_locks;

#endif
