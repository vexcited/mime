/*
 * the sax2 reader of msxml3.dll and msxml6.dll: ISAXXMLReader and the objects it hands to a content handler, the
 * attributes of an element and the locator. a document is read whole into the tree of the engine, white space kept, and
 * then walked in order, so the handler's calls come after the whole document is known to be well formed: a document that
 * fails to parse gives the error handler its fatal error and the content handler no calls.
 */
#include <xmldom/xmldom.h>

/* the interfaces the reader calls, laid out as in windows' msxml headers. */
typedef struct HContent HContent;
typedef struct HContentVtbl {
    MIME_IUNKNOWN_METHODS(HContent)
    HRESULT(STDMETHODCALLTYPE *putDocumentLocator)(HContent *This, void *locator);
    HRESULT(STDMETHODCALLTYPE *startDocument)(HContent *This);
    HRESULT(STDMETHODCALLTYPE *endDocument)(HContent *This);
    HRESULT(STDMETHODCALLTYPE *startPrefixMapping)(HContent *This, const WCHAR *prefix, int cprefix, const WCHAR *uri,
                                                   int curi);
    HRESULT(STDMETHODCALLTYPE *endPrefixMapping)(HContent *This, const WCHAR *prefix, int cprefix);
    HRESULT(STDMETHODCALLTYPE *startElement)(HContent *This, const WCHAR *uri, int curi, const WCHAR *local,
                                             int clocal, const WCHAR *qname, int cqname, void *attributes);
    HRESULT(STDMETHODCALLTYPE *endElement)(HContent *This, const WCHAR *uri, int curi, const WCHAR *local, int clocal,
                                           const WCHAR *qname, int cqname);
    HRESULT(STDMETHODCALLTYPE *characters)(HContent *This, const WCHAR *chars, int cch);
    HRESULT(STDMETHODCALLTYPE *ignorableWhitespace)(HContent *This, const WCHAR *chars, int cch);
    HRESULT(STDMETHODCALLTYPE *processingInstruction)(HContent *This, const WCHAR *target, int ctarget,
                                                      const WCHAR *data, int cdata);
    HRESULT(STDMETHODCALLTYPE *skippedEntity)(HContent *This, const WCHAR *name, int cname);
} HContentVtbl;
struct HContent {
    const HContentVtbl *lpVtbl;
};

typedef struct HError HError;
typedef struct HErrorVtbl {
    MIME_IUNKNOWN_METHODS(HError)
    HRESULT(STDMETHODCALLTYPE *error)(HError *This, void *locator, const WCHAR *message, HRESULT code);
    HRESULT(STDMETHODCALLTYPE *fatalError)(HError *This, void *locator, const WCHAR *message, HRESULT code);
    HRESULT(STDMETHODCALLTYPE *ignorableWarning)(HError *This, void *locator, const WCHAR *message, HRESULT code);
} HErrorVtbl;
struct HError {
    const HErrorVtbl *lpVtbl;
};

typedef struct HLexical HLexical;
typedef struct HLexicalVtbl {
    MIME_IUNKNOWN_METHODS(HLexical)
    HRESULT(STDMETHODCALLTYPE *startDTD)(HLexical *This, const WCHAR *name, int cname, const WCHAR *publicId,
                                         int cpublic, const WCHAR *systemId, int csystem);
    HRESULT(STDMETHODCALLTYPE *endDTD)(HLexical *This);
    HRESULT(STDMETHODCALLTYPE *startEntity)(HLexical *This, const WCHAR *name, int cname);
    HRESULT(STDMETHODCALLTYPE *endEntity)(HLexical *This, const WCHAR *name, int cname);
    HRESULT(STDMETHODCALLTYPE *startCDATA)(HLexical *This);
    HRESULT(STDMETHODCALLTYPE *endCDATA)(HLexical *This);
    HRESULT(STDMETHODCALLTYPE *comment)(HLexical *This, const WCHAR *chars, int cch);
} HLexicalVtbl;
struct HLexical {
    const HLexicalVtbl *lpVtbl;
};

/* ------------------------------------------------------------------ the guids, in windows' msxml headers */

#define SAX_FEATURES 9
enum {
    F_NAMESPACES,
    F_PREFIXES,
    F_EXT_GENERAL,
    F_EXT_PARAMETER,
    F_PROHIBIT_DTD,
    F_SCHEMA_VALIDATION,
    F_EXHAUSTIVE,
    F_INLINE_SCHEMA,
    F_SERVER_HTTP,
};

static const struct {
    const WCHAR *name;
    UINT index;
} g_features[] = {
    {L"http://xml.org/sax/features/namespaces", F_NAMESPACES},
    {L"http://xml.org/sax/features/namespace-prefixes", F_PREFIXES},
    {L"http://xml.org/sax/features/external-general-entities", F_EXT_GENERAL},
    {L"http://xml.org/sax/features/external-parameter-entities", F_EXT_PARAMETER},
    {L"prohibit-dtd", F_PROHIBIT_DTD},
    {L"schema-validation", F_SCHEMA_VALIDATION},
    {L"exhaustive-errors", F_EXHAUSTIVE},
    {L"use-inline-schema", F_INLINE_SCHEMA},
    {L"server-http-request", F_SERVER_HTTP},
};

#define P_LEXICAL L"http://xml.org/sax/properties/lexical-handler"
#define P_DECLARATION L"http://xml.org/sax/properties/declaration-handler"

static BOOL same(const WCHAR *a, const WCHAR *b)
{
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return *a == *b;
}

/* ------------------------------------------------------------------ the locator */

typedef struct SaxLocator {
    const void *vtbl;
    LONG refs;
    LONG line;
    LONG column;
    WCHAR *system_id;
} SaxLocator;

typedef struct SaxLocatorVtbl {
    MIME_IUNKNOWN_METHODS(SaxLocator)
    HRESULT(STDMETHODCALLTYPE *getColumnNumber)(SaxLocator *This, int *column);
    HRESULT(STDMETHODCALLTYPE *getLineNumber)(SaxLocator *This, int *line);
    HRESULT(STDMETHODCALLTYPE *getPublicId)(SaxLocator *This, const WCHAR **id);
    HRESULT(STDMETHODCALLTYPE *getSystemId)(SaxLocator *This, const WCHAR **id);
} SaxLocatorVtbl;

static HRESULT STDMETHODCALLTYPE loc_qi(SaxLocator *l, REFIID riid, void **out)
{
    if (!out)
        return E_POINTER;
    *out = nullptr;
    if (!IsEqualIID(riid, &IID_IUnknown) && !IsEqualIID(riid, &IID_ISAXLocator))
        return E_NOINTERFACE;
    InterlockedIncrement(&l->refs);
    *out = l;
    return S_OK;
}

static ULONG STDMETHODCALLTYPE loc_addref(SaxLocator *l)
{
    return (ULONG)InterlockedIncrement(&l->refs);
}

static ULONG STDMETHODCALLTYPE loc_release(SaxLocator *l)
{
    LONG refs = InterlockedDecrement(&l->refs);
    if (!refs) {
        xd_free(l->system_id);
        xd_free(l);
        InterlockedDecrement(&xd_objects);
    }
    return (ULONG)refs;
}

static HRESULT STDMETHODCALLTYPE loc_column(SaxLocator *l, int *p)
{
    if (!p)
        return E_POINTER;
    *p = l->column;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE loc_line(SaxLocator *l, int *p)
{
    if (!p)
        return E_POINTER;
    *p = l->line;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE loc_public(SaxLocator *l, const WCHAR **p)
{
    if (!p)
        return E_POINTER;
    *p = L"";
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE loc_system(SaxLocator *l, const WCHAR **p)
{
    if (!p)
        return E_POINTER;
    *p = l->system_id ? l->system_id : L"";
    return S_OK;
}

static const SaxLocatorVtbl locator_vtbl = {loc_qi, loc_addref, loc_release, loc_column, loc_line, loc_public,
                                            loc_system};

static SaxLocator *locator_new(const WCHAR *system_id)
{
    SaxLocator *l = xd_alloc(sizeof(SaxLocator));
    if (!l)
        return nullptr;
    l->vtbl = &locator_vtbl;
    l->refs = 1;
    if (system_id)
        l->system_id = xd_wcsdup(system_id, (UINT)-1);
    InterlockedIncrement(&xd_objects);
    return l;
}

/* ------------------------------------------------------------------ the attributes of an element */

typedef struct SaxAttr {
    WCHAR *uri, *local, *qname, *value;
    int ulen, llen, qlen, vlen;
} SaxAttr;

typedef struct SaxAttrs {
    const void *vtbl;
    LONG refs;
    UINT count;
    SaxAttr *a;
} SaxAttrs;

typedef struct SaxAttrsVtbl {
    MIME_IUNKNOWN_METHODS(SaxAttrs)
    HRESULT(STDMETHODCALLTYPE *getLength)(SaxAttrs *This, int *length);
    HRESULT(STDMETHODCALLTYPE *getURI)(SaxAttrs *This, int index, const WCHAR **uri, int *length);
    HRESULT(STDMETHODCALLTYPE *getLocalName)(SaxAttrs *This, int index, const WCHAR **name, int *length);
    HRESULT(STDMETHODCALLTYPE *getQName)(SaxAttrs *This, int index, const WCHAR **name, int *length);
    HRESULT(STDMETHODCALLTYPE *getName)(SaxAttrs *This, int index, const WCHAR **uri, int *ulen, const WCHAR **local,
                                        int *llen, const WCHAR **qname, int *qlen);
    HRESULT(STDMETHODCALLTYPE *getIndexFromName)(SaxAttrs *This, const WCHAR *uri, int ulen, const WCHAR *local,
                                                 int llen, int *index);
    HRESULT(STDMETHODCALLTYPE *getIndexFromQName)(SaxAttrs *This, const WCHAR *qname, int qlen, int *index);
    HRESULT(STDMETHODCALLTYPE *getType)(SaxAttrs *This, int index, const WCHAR **type, int *length);
    HRESULT(STDMETHODCALLTYPE *getTypeFromName)(SaxAttrs *This, const WCHAR *uri, int ulen, const WCHAR *local,
                                                int llen, const WCHAR **type, int *length);
    HRESULT(STDMETHODCALLTYPE *getTypeFromQName)(SaxAttrs *This, const WCHAR *qname, int qlen, const WCHAR **type,
                                                 int *length);
    HRESULT(STDMETHODCALLTYPE *getValue)(SaxAttrs *This, int index, const WCHAR **value, int *length);
    HRESULT(STDMETHODCALLTYPE *getValueFromName)(SaxAttrs *This, const WCHAR *uri, int ulen, const WCHAR *local,
                                                 int llen, const WCHAR **value, int *length);
    HRESULT(STDMETHODCALLTYPE *getValueFromQName)(SaxAttrs *This, const WCHAR *qname, int qlen, const WCHAR **value,
                                                  int *length);
} SaxAttrsVtbl;

static HRESULT STDMETHODCALLTYPE at_qi(SaxAttrs *a, REFIID riid, void **out)
{
    if (!out)
        return E_POINTER;
    *out = nullptr;
    if (!IsEqualIID(riid, &IID_IUnknown) && !IsEqualIID(riid, &IID_ISAXAttributes))
        return E_NOINTERFACE;
    InterlockedIncrement(&a->refs);
    *out = a;
    return S_OK;
}

static ULONG STDMETHODCALLTYPE at_addref(SaxAttrs *a)
{
    return (ULONG)InterlockedIncrement(&a->refs);
}

static ULONG STDMETHODCALLTYPE at_release(SaxAttrs *a)
{
    LONG refs = InterlockedDecrement(&a->refs);
    if (!refs) {
        for (UINT i = 0; i < a->count; i++) {
            xd_free(a->a[i].uri);
            xd_free(a->a[i].local);
            xd_free(a->a[i].qname);
            xd_free(a->a[i].value);
        }
        xd_free(a->a);
        xd_free(a);
        InterlockedDecrement(&xd_objects);
    }
    return (ULONG)refs;
}

static HRESULT STDMETHODCALLTYPE at_length(SaxAttrs *a, int *p)
{
    if (!p)
        return E_POINTER;
    *p = (int)a->count;
    return S_OK;
}

static HRESULT index_ok(SaxAttrs *a, int index, const void *p1, const void *p2)
{
    if (!p1 || !p2)
        return E_POINTER;
    return index < 0 || (UINT)index >= a->count ? E_INVALIDARG : S_OK;
}

static HRESULT STDMETHODCALLTYPE at_uri(SaxAttrs *a, int index, const WCHAR **s, int *n)
{
    HRESULT hr = index_ok(a, index, s, n);
    if (SUCCEEDED(hr)) {
        *s = a->a[index].uri;
        *n = a->a[index].ulen;
    }
    return hr;
}

static HRESULT STDMETHODCALLTYPE at_local(SaxAttrs *a, int index, const WCHAR **s, int *n)
{
    HRESULT hr = index_ok(a, index, s, n);
    if (SUCCEEDED(hr)) {
        *s = a->a[index].local;
        *n = a->a[index].llen;
    }
    return hr;
}

static HRESULT STDMETHODCALLTYPE at_qname(SaxAttrs *a, int index, const WCHAR **s, int *n)
{
    HRESULT hr = index_ok(a, index, s, n);
    if (SUCCEEDED(hr)) {
        *s = a->a[index].qname;
        *n = a->a[index].qlen;
    }
    return hr;
}

static HRESULT STDMETHODCALLTYPE at_name(SaxAttrs *a, int index, const WCHAR **uri, int *ulen, const WCHAR **local,
                                         int *llen, const WCHAR **qname, int *qlen)
{
    if (!uri || !ulen || !local || !llen || !qname || !qlen)
        return E_POINTER;
    if (index < 0 || (UINT)index >= a->count)
        return E_INVALIDARG;
    SaxAttr *x = &a->a[index];
    *uri = x->uri;
    *ulen = x->ulen;
    *local = x->local;
    *llen = x->llen;
    *qname = x->qname;
    *qlen = x->qlen;
    return S_OK;
}

static int find_name(SaxAttrs *a, const WCHAR *uri, int ulen, const WCHAR *local, int llen)
{
    for (UINT i = 0; i < a->count; i++) {
        SaxAttr *x = &a->a[i];
        if (x->ulen == ulen && x->llen == llen && !memcmp(x->uri, uri, (SIZE_T)ulen * sizeof(WCHAR)) &&
            !memcmp(x->local, local, (SIZE_T)llen * sizeof(WCHAR)))
            return (int)i;
    }
    return -1;
}

static int find_qname(SaxAttrs *a, const WCHAR *qname, int qlen)
{
    for (UINT i = 0; i < a->count; i++)
        if (a->a[i].qlen == qlen && !memcmp(a->a[i].qname, qname, (SIZE_T)qlen * sizeof(WCHAR)))
            return (int)i;
    return -1;
}

static HRESULT STDMETHODCALLTYPE at_index_name(SaxAttrs *a, const WCHAR *uri, int ulen, const WCHAR *local, int llen,
                                               int *index)
{
    if (!uri || !local || !index)
        return E_POINTER;
    *index = find_name(a, uri, ulen, local, llen);
    return *index < 0 ? E_INVALIDARG : S_OK;
}

static HRESULT STDMETHODCALLTYPE at_index_qname(SaxAttrs *a, const WCHAR *qname, int qlen, int *index)
{
    if (!qname || !index)
        return E_POINTER;
    *index = find_qname(a, qname, qlen);
    return *index < 0 ? E_INVALIDARG : S_OK;
}

/* every attribute is of the type CDATA: no document type declares any other. */
static HRESULT STDMETHODCALLTYPE at_type(SaxAttrs *a, int index, const WCHAR **s, int *n)
{
    HRESULT hr = index_ok(a, index, s, n);
    if (SUCCEEDED(hr)) {
        *s = L"CDATA";
        *n = 5;
    }
    return hr;
}

static HRESULT STDMETHODCALLTYPE at_type_name(SaxAttrs *a, const WCHAR *uri, int ulen, const WCHAR *local, int llen,
                                              const WCHAR **s, int *n)
{
    if (!uri || !local || !s || !n)
        return E_POINTER;
    if (find_name(a, uri, ulen, local, llen) < 0)
        return E_INVALIDARG;
    *s = L"CDATA";
    *n = 5;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE at_type_qname(SaxAttrs *a, const WCHAR *qname, int qlen, const WCHAR **s, int *n)
{
    if (!qname || !s || !n)
        return E_POINTER;
    if (find_qname(a, qname, qlen) < 0)
        return E_INVALIDARG;
    *s = L"CDATA";
    *n = 5;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE at_value(SaxAttrs *a, int index, const WCHAR **s, int *n)
{
    HRESULT hr = index_ok(a, index, s, n);
    if (SUCCEEDED(hr)) {
        *s = a->a[index].value;
        *n = a->a[index].vlen;
    }
    return hr;
}

static HRESULT STDMETHODCALLTYPE at_value_name(SaxAttrs *a, const WCHAR *uri, int ulen, const WCHAR *local, int llen,
                                               const WCHAR **s, int *n)
{
    if (!uri || !local || !s || !n)
        return E_POINTER;
    int i = find_name(a, uri, ulen, local, llen);
    if (i < 0)
        return E_INVALIDARG;
    *s = a->a[i].value;
    *n = a->a[i].vlen;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE at_value_qname(SaxAttrs *a, const WCHAR *qname, int qlen, const WCHAR **s, int *n)
{
    if (!qname || !s || !n)
        return E_POINTER;
    int i = find_qname(a, qname, qlen);
    if (i < 0)
        return E_INVALIDARG;
    *s = a->a[i].value;
    *n = a->a[i].vlen;
    return S_OK;
}

static const SaxAttrsVtbl attrs_vtbl = {at_qi,         at_addref,    at_release,    at_length,      at_uri,
                                        at_local,      at_qname,     at_name,       at_index_name,  at_index_qname,
                                        at_type,       at_type_name, at_type_qname, at_value,       at_value_name,
                                        at_value_qname};

static WCHAR *copy_part(const WCHAR *s, UINT length)
{
    return xd_wcsdup(s ? s : L"", s ? length : 0);
}

/* the attributes of an element as an object that outlives the tree. namespace declarations are not attributes unless
 * the reader was asked for namespace prefixes. */
static SaxAttrs *attrs_new(XNode *e, BOOL declarations, BOOL namespaces)
{
    SaxAttrs *at = xd_alloc(sizeof(SaxAttrs));
    if (!at)
        return nullptr;
    at->vtbl = &attrs_vtbl;
    at->refs = 1;
    InterlockedIncrement(&xd_objects);
    UINT count = 0;
    for (XNode *a = e->attrs; a; a = a->next)
        if (declarations || !xd_is_xmlns_attr(a))
            count++;
    if (count) {
        at->a = xd_alloc(sizeof(SaxAttr) * count);
        if (!at->a) {
            at_release(at);
            return nullptr;
        }
    }
    for (XNode *a = e->attrs; a; a = a->next) {
        if (!declarations && xd_is_xmlns_attr(a))
            continue;
        SaxAttr *x = &at->a[at->count++];
        UINT q = xd_atom_len(a->qname);
        x->qname = copy_part(a->qname, q);
        x->qlen = (int)q;
        if (namespaces) {
            UINT l = xd_local_length(a);
            x->local = copy_part(xd_local(a), l);
            x->llen = (int)l;
            UINT u = a->uri ? xd_atom_len(a->uri) : 0;
            x->uri = copy_part(a->uri, u);
            x->ulen = (int)u;
        } else {
            x->local = copy_part(nullptr, 0);
            x->uri = copy_part(nullptr, 0);
        }
        x->value = copy_part(a->value, a->length);
        x->vlen = (int)a->length;
        if (!x->qname || !x->local || !x->uri || !x->value) {
            at_release(at);
            return nullptr;
        }
    }
    return at;
}

/* ------------------------------------------------------------------ the reader */

typedef struct SaxReader {
    const void *vtbl;
    LONG refs;
    UINT flags; /* the XDF_ bits of the class. */
    BOOL feature[SAX_FEATURES];
    HContent *content;
    HError *error;
    HLexical *lexical;
    IUnknown *dtd;
    IUnknown *entity;
    IUnknown *declaration;
    WCHAR *base_url;
    WCHAR *secure_base_url;
    WCHAR *decl_version;
    WCHAR *decl_encoding;
    WCHAR *decl_standalone;
} SaxReader;

typedef struct SaxReaderVtbl {
    MIME_IUNKNOWN_METHODS(SaxReader)
    HRESULT(STDMETHODCALLTYPE *getFeature)(SaxReader *This, const WCHAR *name, VARIANT_BOOL *value);
    HRESULT(STDMETHODCALLTYPE *putFeature)(SaxReader *This, const WCHAR *name, VARIANT_BOOL value);
    HRESULT(STDMETHODCALLTYPE *getProperty)(SaxReader *This, const WCHAR *name, VARIANT *value);
    HRESULT(STDMETHODCALLTYPE *putProperty)(SaxReader *This, const WCHAR *name, VARIANT value);
    HRESULT(STDMETHODCALLTYPE *getEntityResolver)(SaxReader *This, IUnknown **resolver);
    HRESULT(STDMETHODCALLTYPE *putEntityResolver)(SaxReader *This, IUnknown *resolver);
    HRESULT(STDMETHODCALLTYPE *getContentHandler)(SaxReader *This, HContent **handler);
    HRESULT(STDMETHODCALLTYPE *putContentHandler)(SaxReader *This, HContent *handler);
    HRESULT(STDMETHODCALLTYPE *getDTDHandler)(SaxReader *This, IUnknown **handler);
    HRESULT(STDMETHODCALLTYPE *putDTDHandler)(SaxReader *This, IUnknown *handler);
    HRESULT(STDMETHODCALLTYPE *getErrorHandler)(SaxReader *This, HError **handler);
    HRESULT(STDMETHODCALLTYPE *putErrorHandler)(SaxReader *This, HError *handler);
    HRESULT(STDMETHODCALLTYPE *getBaseURL)(SaxReader *This, const WCHAR **url);
    HRESULT(STDMETHODCALLTYPE *putBaseURL)(SaxReader *This, const WCHAR *url);
    HRESULT(STDMETHODCALLTYPE *getSecureBaseURL)(SaxReader *This, const WCHAR **url);
    HRESULT(STDMETHODCALLTYPE *putSecureBaseURL)(SaxReader *This, const WCHAR *url);
    HRESULT(STDMETHODCALLTYPE *parse)(SaxReader *This, VARIANT input);
    HRESULT(STDMETHODCALLTYPE *parseURL)(SaxReader *This, const WCHAR *url);
} SaxReaderVtbl;

static void set_object(IUnknown **slot, IUnknown *object)
{
    if (object)
        object->lpVtbl->AddRef(object);
    IUnknown *old = *slot;
    *slot = object;
    if (old)
        old->lpVtbl->Release(old);
}

static HRESULT STDMETHODCALLTYPE rd_qi(SaxReader *r, REFIID riid, void **out)
{
    if (!out)
        return E_POINTER;
    *out = nullptr;
    if (!IsEqualIID(riid, &IID_IUnknown) && !IsEqualIID(riid, &IID_ISAXXMLReader))
        return E_NOINTERFACE;
    InterlockedIncrement(&r->refs);
    *out = r;
    return S_OK;
}

static ULONG STDMETHODCALLTYPE rd_addref(SaxReader *r)
{
    return (ULONG)InterlockedIncrement(&r->refs);
}

static ULONG STDMETHODCALLTYPE rd_release(SaxReader *r)
{
    LONG refs = InterlockedDecrement(&r->refs);
    if (!refs) {
        set_object((IUnknown **)&r->content, nullptr);
        set_object((IUnknown **)&r->error, nullptr);
        set_object((IUnknown **)&r->lexical, nullptr);
        set_object(&r->dtd, nullptr);
        set_object(&r->entity, nullptr);
        set_object(&r->declaration, nullptr);
        xd_free(r->base_url);
        xd_free(r->secure_base_url);
        xd_free(r->decl_version);
        xd_free(r->decl_encoding);
        xd_free(r->decl_standalone);
        xd_free(r);
        InterlockedDecrement(&xd_objects);
    }
    return (ULONG)refs;
}

static int feature_index(const WCHAR *name)
{
    for (UINT i = 0; i < sizeof(g_features) / sizeof(g_features[0]); i++)
        if (same(name, g_features[i].name))
            return (int)g_features[i].index;
    return -1;
}

static HRESULT STDMETHODCALLTYPE rd_get_feature(SaxReader *r, const WCHAR *name, VARIANT_BOOL *value)
{
    if (!name || !value)
        return E_INVALIDARG;
    int i = feature_index(name);
    if (i < 0)
        return E_INVALIDARG;
    *value = r->feature[i] ? VARIANT_TRUE : VARIANT_FALSE;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE rd_put_feature(SaxReader *r, const WCHAR *name, VARIANT_BOOL value)
{
    if (!name)
        return E_INVALIDARG;
    int i = feature_index(name);
    if (i < 0)
        return E_INVALIDARG;
    r->feature[i] = value != VARIANT_FALSE;
    return S_OK;
}

static IUnknown *variant_object(const VARIANT *v)
{
    v = xv_deref(v);
    if (!v)
        return nullptr;
    if ((v->vt & ~VT_BYREF) != VT_UNKNOWN && (v->vt & ~VT_BYREF) != VT_DISPATCH)
        return nullptr;
    return (v->vt & VT_BYREF) ? (v->ppunkVal ? *v->ppunkVal : nullptr) : v->punkVal;
}

static HRESULT STDMETHODCALLTYPE rd_get_property(SaxReader *r, const WCHAR *name, VARIANT *value)
{
    if (!name || !value)
        return E_INVALIDARG;
    VariantInit(value);
    IUnknown *object = nullptr;
    const WCHAR *text = nullptr;
    if (same(name, P_LEXICAL))
        object = (IUnknown *)r->lexical;
    else if (same(name, P_DECLARATION))
        object = r->declaration;
    else if (same(name, L"xmldecl-version"))
        text = r->decl_version;
    else if (same(name, L"xmldecl-encoding"))
        text = r->decl_encoding;
    else if (same(name, L"xmldecl-standalone"))
        text = r->decl_standalone;
    else if (same(name, L"schema-declaration-handler") || same(name, L"schemas") || same(name, L"max-xml-size") ||
             same(name, L"max-element-depth"))
        return S_OK;
    else
        return E_INVALIDARG;
    if (object) {
        object->lpVtbl->AddRef(object);
        value->vt = VT_UNKNOWN;
        value->punkVal = object;
    } else if (text) {
        value->vt = VT_BSTR;
        value->bstrVal = xd_bstr(text, (UINT)-1);
    }
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE rd_put_property(SaxReader *r, const WCHAR *name, VARIANT value)
{
    if (!name)
        return E_INVALIDARG;
    BOOL lexical = same(name, P_LEXICAL);
    if (lexical || same(name, P_DECLARATION)) {
        IUnknown *object = variant_object(&value);
        IUnknown *found = nullptr;
        if (object) {
            HRESULT hr = object->lpVtbl->QueryInterface(object, lexical ? &IID_ISAXLexicalHandler : &IID_ISAXDeclHandler,
                                                        (void **)&found);
            if (FAILED(hr))
                return hr;
        } else if (!xv_is_missing(&value)) {
            return E_INVALIDARG;
        }
        set_object(lexical ? (IUnknown **)&r->lexical : &r->declaration, found);
        if (found)
            found->lpVtbl->Release(found);
        return S_OK;
    }
    if (same(name, L"schema-declaration-handler") || same(name, L"schemas") || same(name, L"max-xml-size") ||
        same(name, L"max-element-depth"))
        return S_OK;
    return E_INVALIDARG;
}

static HRESULT STDMETHODCALLTYPE rd_get_entity(SaxReader *r, IUnknown **p)
{
    if (!p)
        return E_POINTER;
    *p = r->entity;
    if (*p)
        (*p)->lpVtbl->AddRef(*p);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE rd_put_entity(SaxReader *r, IUnknown *p)
{
    set_object(&r->entity, p);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE rd_get_content(SaxReader *r, HContent **p)
{
    if (!p)
        return E_POINTER;
    *p = r->content;
    if (*p)
        (*p)->lpVtbl->AddRef(*p);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE rd_put_content(SaxReader *r, HContent *p)
{
    set_object((IUnknown **)&r->content, (IUnknown *)p);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE rd_get_dtd(SaxReader *r, IUnknown **p)
{
    if (!p)
        return E_POINTER;
    *p = r->dtd;
    if (*p)
        (*p)->lpVtbl->AddRef(*p);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE rd_put_dtd(SaxReader *r, IUnknown *p)
{
    set_object(&r->dtd, p);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE rd_get_error(SaxReader *r, HError **p)
{
    if (!p)
        return E_POINTER;
    *p = r->error;
    if (*p)
        (*p)->lpVtbl->AddRef(*p);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE rd_put_error(SaxReader *r, HError *p)
{
    set_object((IUnknown **)&r->error, (IUnknown *)p);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE rd_get_base(SaxReader *r, const WCHAR **p)
{
    if (!p)
        return E_POINTER;
    *p = r->base_url ? r->base_url : L"";
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE rd_put_base(SaxReader *r, const WCHAR *url)
{
    WCHAR *copy = url ? xd_wcsdup(url, (UINT)-1) : nullptr;
    if (url && !copy)
        return E_OUTOFMEMORY;
    xd_free(r->base_url);
    r->base_url = copy;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE rd_get_secure_base(SaxReader *r, const WCHAR **p)
{
    if (!p)
        return E_POINTER;
    *p = r->secure_base_url ? r->secure_base_url : L"";
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE rd_put_secure_base(SaxReader *r, const WCHAR *url)
{
    WCHAR *copy = url ? xd_wcsdup(url, (UINT)-1) : nullptr;
    if (url && !copy)
        return E_OUTOFMEMORY;
    xd_free(r->secure_base_url);
    r->secure_base_url = copy;
    return S_OK;
}

/* ------------------------------------------------------------------ walking a parsed document */

typedef struct Walk {
    SaxReader *r;
    XDoc *doc;
    SaxLocator *locator;
    BOOL namespaces;
    BOOL declarations;
} Walk;

#define CONTENT(w) ((w)->r->content)
#define LEXICAL(w) ((w)->r->lexical)

/* the prefix of a namespace declaration, which is empty for the default one. */
static const WCHAR *declared_prefix(const XNode *a, int *length)
{
    UINT q = xd_atom_len(a->qname);
    if (q == 5) {
        *length = 0;
        return L"";
    }
    *length = (int)(q - 6);
    return a->qname + 6;
}

static HRESULT enter_element(Walk *w, XNode *e)
{
    HContent *h = CONTENT(w);
    if (!h)
        return S_OK;
    HRESULT hr = S_OK;
    if (w->namespaces) {
        for (XNode *a = e->attrs; a && SUCCEEDED(hr); a = a->next) {
            if (!xd_is_xmlns_attr(a))
                continue;
            int plen;
            const WCHAR *prefix = declared_prefix(a, &plen);
            hr = h->lpVtbl->startPrefixMapping(h, prefix, plen, a->value ? a->value : L"", (int)a->length);
        }
        if (FAILED(hr))
            return hr;
    }
    SaxAttrs *attrs = attrs_new(e, w->declarations, w->namespaces);
    if (!attrs)
        return E_OUTOFMEMORY;
    UINT q = xd_atom_len(e->qname);
    const WCHAR *uri = L"";
    const WCHAR *local = L"";
    int ulen = 0, llen = 0;
    if (w->namespaces) {
        uri = e->uri ? e->uri : L"";
        ulen = e->uri ? (int)xd_atom_len(e->uri) : 0;
        local = xd_local(e);
        llen = (int)xd_local_length(e);
    }
    hr = h->lpVtbl->startElement(h, uri, ulen, local, llen, e->qname, (int)q, attrs);
    at_release(attrs);
    return hr;
}

static HRESULT leave_element(Walk *w, XNode *e)
{
    HContent *h = CONTENT(w);
    if (!h)
        return S_OK;
    UINT q = xd_atom_len(e->qname);
    const WCHAR *uri = L"";
    const WCHAR *local = L"";
    int ulen = 0, llen = 0;
    if (w->namespaces) {
        uri = e->uri ? e->uri : L"";
        ulen = e->uri ? (int)xd_atom_len(e->uri) : 0;
        local = xd_local(e);
        llen = (int)xd_local_length(e);
    }
    HRESULT hr = h->lpVtbl->endElement(h, uri, ulen, local, llen, e->qname, (int)q);
    if (FAILED(hr) || !w->namespaces)
        return hr;
    XNode *last = e->attrs;
    while (last && last->next)
        last = last->next;
    for (XNode *a = last; a && SUCCEEDED(hr); a = a->prev) {
        if (!xd_is_xmlns_attr(a))
            continue;
        int plen;
        const WCHAR *prefix = declared_prefix(a, &plen);
        hr = h->lpVtbl->endPrefixMapping(h, prefix, plen);
    }
    return hr;
}

static HRESULT enter_node(Walk *w, XNode *n)
{
    HContent *h = CONTENT(w);
    switch (n->type) {
    case XN_ELEMENT:
        return enter_element(w, n);
    case XN_TEXT:
        if (h && n->length)
            return h->lpVtbl->characters(h, n->value, (int)n->length);
        break;
    case XN_CDATA: {
        HRESULT hr = S_OK;
        HLexical *lex = LEXICAL(w);
        if (lex)
            hr = lex->lpVtbl->startCDATA(lex);
        if (SUCCEEDED(hr) && h && n->length)
            hr = h->lpVtbl->characters(h, n->value, (int)n->length);
        if (SUCCEEDED(hr) && lex)
            hr = lex->lpVtbl->endCDATA(lex);
        return hr;
    }
    case XN_PI:
        if (h && !(n->flags & XF_XMLDECL))
            return h->lpVtbl->processingInstruction(h, n->qname, (int)xd_atom_len(n->qname), n->value ? n->value : L"",
                                                    (int)n->length);
        break;
    case XN_COMMENT: {
        HLexical *lex = LEXICAL(w);
        if (lex)
            return lex->lpVtbl->comment(lex, n->value ? n->value : L"", (int)n->length);
        break;
    }
    }
    return S_OK;
}

static HRESULT walk_document(Walk *w)
{
    HContent *h = CONTENT(w);
    HRESULT hr = S_OK;
    if (h) {
        hr = h->lpVtbl->putDocumentLocator(h, w->locator);
        if (SUCCEEDED(hr))
            hr = h->lpVtbl->startDocument(h);
    }
    XNode *n = w->doc->node.first;
    while (n && SUCCEEDED(hr)) {
        hr = enter_node(w, n);
        if (FAILED(hr))
            break;
        if (n->first && (n->type == XN_ELEMENT || n->type == XN_ENTITYREF)) {
            n = n->first;
            continue;
        }
        for (;;) {
            if (n->type == XN_ELEMENT) {
                hr = leave_element(w, n);
                if (FAILED(hr))
                    break;
            }
            if (n->next) {
                n = n->next;
                break;
            }
            n = n->parent;
            if (!n || n == &w->doc->node) {
                n = nullptr;
                break;
            }
        }
    }
    if (SUCCEEDED(hr) && h)
        hr = h->lpVtbl->endDocument(h);
    return hr;
}

/* a parse that failed: the error handler is told, and the parse fails with the code of the error. */
static HRESULT report_failure(SaxReader *r, XDoc *doc, SaxLocator *locator)
{
    HRESULT code = doc->err.code ? (HRESULT)doc->err.code : E_FAIL;
    locator->line = doc->err.line;
    locator->column = doc->err.linepos;
    if (r->error) {
        const WCHAR *reason = doc->err.reason ? doc->err.reason : L"";
        r->error->lpVtbl->fatalError(r->error, locator, reason, code);
    }
    return code;
}

static void keep_declaration(SaxReader *r, XDoc *doc)
{
    xd_free(r->decl_version);
    xd_free(r->decl_encoding);
    xd_free(r->decl_standalone);
    const WCHAR *v = xd_decl_attr(doc, "version");
    const WCHAR *e = xd_decl_attr(doc, "encoding");
    const WCHAR *s = xd_decl_attr(doc, "standalone");
    r->decl_version = v ? xd_wcsdup(v, (UINT)-1) : nullptr;
    r->decl_encoding = e ? xd_wcsdup(e, (UINT)-1) : nullptr;
    r->decl_standalone = s ? xd_wcsdup(s, (UINT)-1) : nullptr;
}

/* 0 text, 1 bytes, 2 a url. */
static HRESULT run(SaxReader *r, int kind, const WCHAR *text, UINT length, const BYTE *bytes, UINT size,
                   const WCHAR *url)
{
    UINT flags = XDF_PRESERVE | (r->flags & XDF_V6);
    if (r->feature[F_PROHIBIT_DTD])
        flags |= XDF_PROHIBIT_DTD;
    XDoc *doc = xd_doc_new(flags);
    if (!doc)
        return E_OUTOFMEMORY;
    SaxLocator *locator = locator_new(kind == 2 ? url : nullptr);
    if (!locator) {
        xn_release(&doc->node);
        return E_OUTOFMEMORY;
    }
    BOOL ok;
    if (kind == 0)
        ok = xd_parse(doc, text, length, XP_STRING);
    else if (kind == 1)
        ok = xd_parse_bytes(doc, bytes, size);
    else
        ok = xd_load_file(doc, url);
    HRESULT hr;
    if (!ok) {
        hr = report_failure(r, doc, locator);
    } else {
        Walk w = {r, doc, locator, r->feature[F_NAMESPACES], r->feature[F_PREFIXES]};
        keep_declaration(r, doc);
        hr = walk_document(&w);
    }
    loc_release(locator);
    xn_release(&doc->node);
    return hr;
}

static HRESULT STDMETHODCALLTYPE rd_parse(SaxReader *r, VARIANT input)
{
    const VARIANT *v = xv_deref(&input);
    if (!v)
        return E_INVALIDARG;
    switch (v->vt & ~VT_BYREF) {
    case VT_BSTR: {
        BSTR s = (v->vt & VT_BYREF) ? (v->pbstrVal ? *v->pbstrVal : nullptr) : v->bstrVal;
        if (!s)
            return E_INVALIDARG;
        return run(r, 0, s, SysStringLen(s), nullptr, 0, nullptr);
    }
    case VT_UNKNOWN:
    case VT_DISPATCH: {
        IUnknown *object = variant_object(v);
        if (!object)
            return E_INVALIDARG;
        BYTE *data = nullptr;
        UINT size = 0;
        HRESULT hr = xd_read_stream(object, &data, &size);
        if (FAILED(hr))
            return hr;
        hr = run(r, 1, nullptr, 0, data, size, nullptr);
        xd_free(data);
        return hr;
    }
    case VT_ARRAY | VT_UI1: {
        SAFEARRAY *a = (v->vt & VT_BYREF) ? (v->pparray ? *v->pparray : nullptr) : v->parray;
        if (!a || SafeArrayGetDim(a) != 1)
            return E_INVALIDARG;
        LONG lo, hi;
        void *bytes;
        if (FAILED(SafeArrayGetLBound(a, 1, &lo)) || FAILED(SafeArrayGetUBound(a, 1, &hi)) ||
            FAILED(SafeArrayAccessData(a, &bytes)))
            return E_INVALIDARG;
        UINT size = hi >= lo ? (UINT)(hi - lo + 1) : 0;
        BYTE *data = xd_alloc_raw(size);
        if (data)
            memcpy(data, bytes, size);
        SafeArrayUnaccessData(a);
        if (!data)
            return E_OUTOFMEMORY;
        HRESULT hr = run(r, 1, nullptr, 0, data, size, nullptr);
        xd_free(data);
        return hr;
    }
    }
    return E_INVALIDARG;
}

static HRESULT STDMETHODCALLTYPE rd_parse_url(SaxReader *r, const WCHAR *url)
{
    if (!url || !*url)
        return E_INVALIDARG;
    return run(r, 2, nullptr, 0, nullptr, 0, url);
}

static const SaxReaderVtbl reader_vtbl = {
    rd_qi,         rd_addref,       rd_release,         rd_get_feature,     rd_put_feature, rd_get_property,
    rd_put_property, rd_get_entity, rd_put_entity,      rd_get_content,     rd_put_content, rd_get_dtd,
    rd_put_dtd,    rd_get_error,    rd_put_error,       rd_get_base,        rd_put_base,    rd_get_secure_base,
    rd_put_secure_base, rd_parse,   rd_parse_url};

HRESULT xd_create_sax_reader(UINT flags, REFIID riid, void **out)
{
    SaxReader *r = xd_alloc(sizeof(SaxReader));
    if (!r)
        return E_OUTOFMEMORY;
    r->vtbl = &reader_vtbl;
    r->refs = 1;
    r->flags = flags;
    InterlockedIncrement(&xd_objects);
    /* msxml3 reads external entities and allows a document type, msxml6 does neither. */
    BOOL v6 = (flags & XDF_V6) != 0;
    r->feature[F_NAMESPACES] = TRUE;
    r->feature[F_EXT_GENERAL] = !v6;
    r->feature[F_EXT_PARAMETER] = !v6;
    r->feature[F_PROHIBIT_DTD] = v6;
    HRESULT hr = rd_qi(r, riid, out);
    rd_release(r);
    return hr;
}
