/*
 * the tables of functions of the dom's interfaces, in the order windows' typelib gives them. a node of any type
 * shows the table of its type, which starts with the methods of IXMLDOMNode, so the tables only differ in what
 * follows them. the selection of a method by number is what the dispatch tables rely on.
 */
#ifndef MIME_XMLDOM_VTABLES_H
#define MIME_XMLDOM_VTABLES_H

#include <stddef.h>

#define XSLOT(vtbl, member) ((WORD)(offsetof(vtbl, member) / sizeof(void *)))

#define XM_NODE(T)                                                                                                     \
    IDispatch_METHODS(T)                                                                                               \
    HRESULT(STDMETHODCALLTYPE *get_nodeName)(T * This, BSTR * p);                                                      \
    HRESULT(STDMETHODCALLTYPE *get_nodeValue)(T * This, VARIANT * value);                                              \
    HRESULT(STDMETHODCALLTYPE *put_nodeValue)(T * This, VARIANT value);                                                \
    HRESULT(STDMETHODCALLTYPE *get_nodeType)(T * This, LONG * type);                                                   \
    HRESULT(STDMETHODCALLTYPE *get_parentNode)(T * This, void **parent);                                               \
    HRESULT(STDMETHODCALLTYPE *get_childNodes)(T * This, void **list);                                                 \
    HRESULT(STDMETHODCALLTYPE *get_firstChild)(T * This, void **node);                                                 \
    HRESULT(STDMETHODCALLTYPE *get_lastChild)(T * This, void **node);                                                  \
    HRESULT(STDMETHODCALLTYPE *get_previousSibling)(T * This, void **node);                                            \
    HRESULT(STDMETHODCALLTYPE *get_nextSibling)(T * This, void **node);                                                \
    HRESULT(STDMETHODCALLTYPE *get_attributes)(T * This, void **map);                                                  \
    HRESULT(STDMETHODCALLTYPE *insertBefore)(T * This, T * node, VARIANT before, void **old);                          \
    HRESULT(STDMETHODCALLTYPE *replaceChild)(T * This, T * node, T * old, void **out);                                 \
    HRESULT(STDMETHODCALLTYPE *removeChild)(T * This, T * node, void **out);                                           \
    HRESULT(STDMETHODCALLTYPE *appendChild)(T * This, T * node, void **out);                                           \
    HRESULT(STDMETHODCALLTYPE *hasChildNodes)(T * This, VARIANT_BOOL * has);                                           \
    HRESULT(STDMETHODCALLTYPE *get_ownerDocument)(T * This, void **doc);                                               \
    HRESULT(STDMETHODCALLTYPE *cloneNode)(T * This, VARIANT_BOOL deep, void **out);                                    \
    HRESULT(STDMETHODCALLTYPE *get_nodeTypeString)(T * This, BSTR * p);                                                \
    HRESULT(STDMETHODCALLTYPE *get_text)(T * This, BSTR * p);                                                          \
    HRESULT(STDMETHODCALLTYPE *put_text)(T * This, BSTR p);                                                            \
    HRESULT(STDMETHODCALLTYPE *get_specified)(T * This, VARIANT_BOOL * p);                                             \
    HRESULT(STDMETHODCALLTYPE *get_definition)(T * This, void **node);                                                 \
    HRESULT(STDMETHODCALLTYPE *get_nodeTypedValue)(T * This, VARIANT * value);                                         \
    HRESULT(STDMETHODCALLTYPE *put_nodeTypedValue)(T * This, VARIANT value);                                           \
    HRESULT(STDMETHODCALLTYPE *get_dataType)(T * This, VARIANT * value);                                               \
    HRESULT(STDMETHODCALLTYPE *put_dataType)(T * This, BSTR p);                                                        \
    HRESULT(STDMETHODCALLTYPE *get_xml)(T * This, BSTR * p);                                                           \
    HRESULT(STDMETHODCALLTYPE *transformNode)(T * This, T * style, BSTR * p);                                          \
    HRESULT(STDMETHODCALLTYPE *selectNodes)(T * This, BSTR query, void **list);                                        \
    HRESULT(STDMETHODCALLTYPE *selectSingleNode)(T * This, BSTR query, void **node);                                   \
    HRESULT(STDMETHODCALLTYPE *get_parsed)(T * This, VARIANT_BOOL * p);                                                \
    HRESULT(STDMETHODCALLTYPE *get_namespaceURI)(T * This, BSTR * p);                                                  \
    HRESULT(STDMETHODCALLTYPE *get_prefix)(T * This, BSTR * p);                                                        \
    HRESULT(STDMETHODCALLTYPE *get_baseName)(T * This, BSTR * p);                                                      \
    HRESULT(STDMETHODCALLTYPE *transformNodeToObject)(T * This, T * style, VARIANT output);

#define XM_DATA(T)                                                                                                     \
    HRESULT(STDMETHODCALLTYPE *get_data)(T * This, BSTR * p);                                                          \
    HRESULT(STDMETHODCALLTYPE *put_data)(T * This, BSTR p);                                                            \
    HRESULT(STDMETHODCALLTYPE *get_length)(T * This, LONG * length);                                                   \
    HRESULT(STDMETHODCALLTYPE *substringData)(T * This, LONG offset, LONG count, BSTR * p);                            \
    HRESULT(STDMETHODCALLTYPE *appendData)(T * This, BSTR p);                                                          \
    HRESULT(STDMETHODCALLTYPE *insertData)(T * This, LONG offset, BSTR p);                                             \
    HRESULT(STDMETHODCALLTYPE *deleteData)(T * This, LONG offset, LONG count);                                         \
    HRESULT(STDMETHODCALLTYPE *replaceData)(T * This, LONG offset, LONG count, BSTR p);

#define XM_DOCUMENT(T)                                                                                                 \
    HRESULT(STDMETHODCALLTYPE *get_doctype)(T * This, void **type);                                                    \
    HRESULT(STDMETHODCALLTYPE *get_implementation)(T * This, void **impl);                                             \
    HRESULT(STDMETHODCALLTYPE *get_documentElement)(T * This, void **element);                                         \
    HRESULT(STDMETHODCALLTYPE *putref_documentElement)(T * This, T * element);                                         \
    HRESULT(STDMETHODCALLTYPE *createElement)(T * This, BSTR name, void **out);                                        \
    HRESULT(STDMETHODCALLTYPE *createDocumentFragment)(T * This, void **out);                                          \
    HRESULT(STDMETHODCALLTYPE *createTextNode)(T * This, BSTR data, void **out);                                       \
    HRESULT(STDMETHODCALLTYPE *createComment)(T * This, BSTR data, void **out);                                        \
    HRESULT(STDMETHODCALLTYPE *createCDATASection)(T * This, BSTR data, void **out);                                   \
    HRESULT(STDMETHODCALLTYPE *createProcessingInstruction)(T * This, BSTR target, BSTR data, void **out);             \
    HRESULT(STDMETHODCALLTYPE *createAttribute)(T * This, BSTR name, void **out);                                      \
    HRESULT(STDMETHODCALLTYPE *createEntityReference)(T * This, BSTR name, void **out);                                \
    HRESULT(STDMETHODCALLTYPE *getElementsByTagName)(T * This, BSTR name, void **list);                                \
    HRESULT(STDMETHODCALLTYPE *createNode)(T * This, VARIANT type, BSTR name, BSTR uri, void **out);                   \
    HRESULT(STDMETHODCALLTYPE *nodeFromID)(T * This, BSTR id, void **out);                                             \
    HRESULT(STDMETHODCALLTYPE *load)(T * This, VARIANT source, VARIANT_BOOL * ok);                                     \
    HRESULT(STDMETHODCALLTYPE *get_readyState)(T * This, LONG * state);                                                \
    HRESULT(STDMETHODCALLTYPE *get_parseError)(T * This, void **error);                                                \
    HRESULT(STDMETHODCALLTYPE *get_url)(T * This, BSTR * url);                                                         \
    HRESULT(STDMETHODCALLTYPE *get_async)(T * This, VARIANT_BOOL * async);                                             \
    HRESULT(STDMETHODCALLTYPE *put_async)(T * This, VARIANT_BOOL async);                                               \
    HRESULT(STDMETHODCALLTYPE *abort)(T * This);                                                                       \
    HRESULT(STDMETHODCALLTYPE *loadXML)(T * This, BSTR xml, VARIANT_BOOL * ok);                                        \
    HRESULT(STDMETHODCALLTYPE *save)(T * This, VARIANT target);                                                        \
    HRESULT(STDMETHODCALLTYPE *get_validateOnParse)(T * This, VARIANT_BOOL * p);                                       \
    HRESULT(STDMETHODCALLTYPE *put_validateOnParse)(T * This, VARIANT_BOOL p);                                         \
    HRESULT(STDMETHODCALLTYPE *get_resolveExternals)(T * This, VARIANT_BOOL * p);                                      \
    HRESULT(STDMETHODCALLTYPE *put_resolveExternals)(T * This, VARIANT_BOOL p);                                        \
    HRESULT(STDMETHODCALLTYPE *get_preserveWhiteSpace)(T * This, VARIANT_BOOL * p);                                    \
    HRESULT(STDMETHODCALLTYPE *put_preserveWhiteSpace)(T * This, VARIANT_BOOL p);                                      \
    HRESULT(STDMETHODCALLTYPE *put_onreadystatechange)(T * This, VARIANT handler);                                     \
    HRESULT(STDMETHODCALLTYPE *put_ondataavailable)(T * This, VARIANT handler);                                        \
    HRESULT(STDMETHODCALLTYPE *put_ontransformnode)(T * This, VARIANT handler);                                        \
    HRESULT(STDMETHODCALLTYPE *get_namespaces)(T * This, void **collection);                                           \
    HRESULT(STDMETHODCALLTYPE *get_schemas)(T * This, VARIANT * schemas);                                              \
    HRESULT(STDMETHODCALLTYPE *putref_schemas)(T * This, VARIANT schemas);                                             \
    HRESULT(STDMETHODCALLTYPE *validate)(T * This, void **error);                                                      \
    HRESULT(STDMETHODCALLTYPE *setProperty)(T * This, BSTR name, VARIANT value);                                       \
    HRESULT(STDMETHODCALLTYPE *getProperty)(T * This, BSTR name, VARIANT * value);                                     \
    HRESULT(STDMETHODCALLTYPE *validateNode)(T * This, T * node, void **error);                                        \
    HRESULT(STDMETHODCALLTYPE *importNode)(T * This, T * node, VARIANT_BOOL deep, void **out);

typedef struct XNodeVtbl {
    XM_NODE(XNode)
} XNodeVtbl;

typedef struct XElementVtbl {
    XM_NODE(XNode)
    HRESULT(STDMETHODCALLTYPE *get_tagName)(XNode *This, BSTR *p);
    HRESULT(STDMETHODCALLTYPE *getAttribute)(XNode *This, BSTR name, VARIANT *value);
    HRESULT(STDMETHODCALLTYPE *setAttribute)(XNode *This, BSTR name, VARIANT value);
    HRESULT(STDMETHODCALLTYPE *removeAttribute)(XNode *This, BSTR name);
    HRESULT(STDMETHODCALLTYPE *getAttributeNode)(XNode *This, BSTR name, void **attribute);
    HRESULT(STDMETHODCALLTYPE *setAttributeNode)(XNode *This, XNode *attribute, void **old);
    HRESULT(STDMETHODCALLTYPE *removeAttributeNode)(XNode *This, XNode *attribute, void **old);
    HRESULT(STDMETHODCALLTYPE *getElementsByTagName)(XNode *This, BSTR name, void **list);
    HRESULT(STDMETHODCALLTYPE *normalize)(XNode *This);
} XElementVtbl;

typedef struct XAttributeVtbl {
    XM_NODE(XNode)
    HRESULT(STDMETHODCALLTYPE *get_name)(XNode *This, BSTR *p);
    HRESULT(STDMETHODCALLTYPE *get_value)(XNode *This, VARIANT *value);
    HRESULT(STDMETHODCALLTYPE *put_value)(XNode *This, VARIANT value);
} XAttributeVtbl;

/* text and cdata sections split, comments only have the character data methods. */
typedef struct XTextVtbl {
    XM_NODE(XNode)
    XM_DATA(XNode)
    HRESULT(STDMETHODCALLTYPE *splitText)(XNode *This, LONG offset, void **second);
} XTextVtbl;

typedef struct XCommentVtbl {
    XM_NODE(XNode)
    XM_DATA(XNode)
} XCommentVtbl;

typedef struct XPIVtbl {
    XM_NODE(XNode)
    HRESULT(STDMETHODCALLTYPE *get_target)(XNode *This, BSTR *p);
    HRESULT(STDMETHODCALLTYPE *get_data)(XNode *This, BSTR *p);
    HRESULT(STDMETHODCALLTYPE *put_data)(XNode *This, BSTR p);
} XPIVtbl;

typedef struct XDocTypeVtbl {
    XM_NODE(XNode)
    HRESULT(STDMETHODCALLTYPE *get_name)(XNode *This, BSTR *p);
    HRESULT(STDMETHODCALLTYPE *get_entities)(XNode *This, void **map);
    HRESULT(STDMETHODCALLTYPE *get_notations)(XNode *This, void **map);
} XDocTypeVtbl;

typedef struct XDocumentVtbl {
    XM_NODE(XNode)
    XM_DOCUMENT(XNode)
} XDocumentVtbl;

/* fragments and entity references add nothing to the node's methods. */

typedef struct XListVtbl {
    IDispatch_METHODS(XList)
    HRESULT(STDMETHODCALLTYPE *get_item)(XList *This, LONG index, void **node);
    HRESULT(STDMETHODCALLTYPE *get_length)(XList *This, LONG *length);
    HRESULT(STDMETHODCALLTYPE *nextNode)(XList *This, void **node);
    HRESULT(STDMETHODCALLTYPE *reset)(XList *This);
    HRESULT(STDMETHODCALLTYPE *get__newEnum)(XList *This, IUnknown **enumerator);
} XListVtbl;

typedef struct XSelectionVtbl {
    IDispatch_METHODS(XList)
    HRESULT(STDMETHODCALLTYPE *get_item)(XList *This, LONG index, void **node);
    HRESULT(STDMETHODCALLTYPE *get_length)(XList *This, LONG *length);
    HRESULT(STDMETHODCALLTYPE *nextNode)(XList *This, void **node);
    HRESULT(STDMETHODCALLTYPE *reset)(XList *This);
    HRESULT(STDMETHODCALLTYPE *get__newEnum)(XList *This, IUnknown **enumerator);
    HRESULT(STDMETHODCALLTYPE *get_expr)(XList *This, BSTR *p);
    HRESULT(STDMETHODCALLTYPE *put_expr)(XList *This, BSTR p);
    HRESULT(STDMETHODCALLTYPE *get_context)(XList *This, void **node);
    HRESULT(STDMETHODCALLTYPE *putref_context)(XList *This, XNode *node);
    HRESULT(STDMETHODCALLTYPE *peekNode)(XList *This, void **node);
    HRESULT(STDMETHODCALLTYPE *matches)(XList *This, XNode *node, void **out);
    HRESULT(STDMETHODCALLTYPE *removeNext)(XList *This, void **node);
    HRESULT(STDMETHODCALLTYPE *removeAll)(XList *This);
    HRESULT(STDMETHODCALLTYPE *clone)(XList *This, void **out);
    HRESULT(STDMETHODCALLTYPE *getProperty)(XList *This, BSTR name, VARIANT *value);
    HRESULT(STDMETHODCALLTYPE *setProperty)(XList *This, BSTR name, VARIANT value);
} XSelectionVtbl;

typedef struct XMapVtbl {
    IDispatch_METHODS(XList)
    HRESULT(STDMETHODCALLTYPE *getNamedItem)(XList *This, BSTR name, void **node);
    HRESULT(STDMETHODCALLTYPE *setNamedItem)(XList *This, XNode *node, void **old);
    HRESULT(STDMETHODCALLTYPE *removeNamedItem)(XList *This, BSTR name, void **node);
    HRESULT(STDMETHODCALLTYPE *get_item)(XList *This, LONG index, void **node);
    HRESULT(STDMETHODCALLTYPE *get_length)(XList *This, LONG *length);
    HRESULT(STDMETHODCALLTYPE *getQualifiedItem)(XList *This, BSTR name, BSTR uri, void **node);
    HRESULT(STDMETHODCALLTYPE *removeQualifiedItem)(XList *This, BSTR name, BSTR uri, void **node);
    HRESULT(STDMETHODCALLTYPE *nextNode)(XList *This, void **node);
    HRESULT(STDMETHODCALLTYPE *reset)(XList *This);
    HRESULT(STDMETHODCALLTYPE *get__newEnum)(XList *This, IUnknown **enumerator);
} XMapVtbl;

/* parse errors, the implementation object and the enumerator are small objects of their own. */
typedef struct XErrObj XErrObj;
struct XErrObj {
    const void *vtbl;
    LONG refs;
    XErr e;
};
typedef struct XErrVtbl {
    IDispatch_METHODS(XErrObj)
    HRESULT(STDMETHODCALLTYPE *get_errorCode)(XErrObj *This, LONG *code);
    HRESULT(STDMETHODCALLTYPE *get_url)(XErrObj *This, BSTR *p);
    HRESULT(STDMETHODCALLTYPE *get_reason)(XErrObj *This, BSTR *p);
    HRESULT(STDMETHODCALLTYPE *get_srcText)(XErrObj *This, BSTR *p);
    HRESULT(STDMETHODCALLTYPE *get_line)(XErrObj *This, LONG *p);
    HRESULT(STDMETHODCALLTYPE *get_linepos)(XErrObj *This, LONG *p);
    HRESULT(STDMETHODCALLTYPE *get_filepos)(XErrObj *This, LONG *p);
    HRESULT(STDMETHODCALLTYPE *get_errorXPath)(XErrObj *This, BSTR *p);
    HRESULT(STDMETHODCALLTYPE *get_allErrors)(XErrObj *This, void **collection);
    HRESULT(STDMETHODCALLTYPE *errorParameters)(XErrObj *This, LONG index, BSTR *p);
    HRESULT(STDMETHODCALLTYPE *get_errorParametersCount)(XErrObj *This, LONG *count);
} XErrVtbl;

typedef struct XImpl {
    const void *vtbl;
    LONG refs;
} XImpl;
typedef struct XImplVtbl {
    IDispatch_METHODS(XImpl)
    HRESULT(STDMETHODCALLTYPE *hasFeature)(XImpl *This, BSTR feature, BSTR version, VARIANT_BOOL *has);
} XImplVtbl;

typedef struct XEnum {
    const void *vtbl;
    LONG refs;
    IUnknown *source; /* the list or map, referred to. */
    UINT pos;
} XEnum;
typedef struct XEnumVtbl {
    MIME_IUNKNOWN_METHODS(XEnum)
    HRESULT(STDMETHODCALLTYPE *Next)(XEnum *This, ULONG count, VARIANT *values, ULONG *fetched);
    HRESULT(STDMETHODCALLTYPE *Skip)(XEnum *This, ULONG count);
    HRESULT(STDMETHODCALLTYPE *Reset)(XEnum *This);
    HRESULT(STDMETHODCALLTYPE *Clone)(XEnum *This, void **out);
} XEnumVtbl;

/* the secondary interfaces of a node: ISupportErrorInfo and IObjectSafety on one small object. */
typedef struct XAux {
    const void *vtbl;
    LONG refs;
    XNode *node;
    BOOL safety;
} XAux;

extern const XNodeVtbl xd_vtbl_node;
extern const XElementVtbl xd_vtbl_element;
extern const XAttributeVtbl xd_vtbl_attribute;
extern const XTextVtbl xd_vtbl_text;
extern const XTextVtbl xd_vtbl_cdata;
extern const XCommentVtbl xd_vtbl_comment;
extern const XPIVtbl xd_vtbl_pi;
extern const XDocTypeVtbl xd_vtbl_doctype;
extern XDocumentVtbl xd_vtbl_document; /* filled in by xd_init. */
extern const XNodeVtbl xd_vtbl_fragment;
extern const XNodeVtbl xd_vtbl_entityref;
extern const XListVtbl xd_vtbl_list;
extern const XSelectionVtbl xd_vtbl_selection;
extern const XMapVtbl xd_vtbl_map;
extern const XErrVtbl xd_vtbl_error;
extern const XImplVtbl xd_vtbl_impl;
extern const XEnumVtbl xd_vtbl_enum;

#endif
