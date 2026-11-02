/*
 * IDispatch for the dom objects. each method or property is a table row: name, dispid, invoke kinds, vtable slot
 * and argument types. Invoke converts variants to and from the vtable call, so late bound callers reach the same
 * code. row arguments are letters:
 *   b bstr, l long, f boolean, n a node, v a variant, w an optional variant,
 * then '>' and the result:
 *   B bstr, L long, F boolean, V variant, N an object, U an unknown, and a minus for nothing.
 */
#include <xmldom/xmldom.h>

#define GET (DISPATCH_PROPERTYGET | DISPATCH_METHOD)
#define CALL (DISPATCH_METHOD | DISPATCH_PROPERTYGET)
#define PUT (DISPATCH_PROPERTYPUT | DISPATCH_PROPERTYPUTREF)

#define ROW(name, id, flags, vtbl, member, sig) {name, id, flags, XSLOT(vtbl, member), sig}

#define DISPID_READYSTATE_ID (-525)
#define DISPID_NEWENUM_ID (-4)

static const XDisp node_rows[] = {
    ROW(L"nodeName", 2, GET, XNodeVtbl, get_nodeName, ">B"),
    ROW(L"nodeValue", 3, GET, XNodeVtbl, get_nodeValue, ">V"),
    ROW(L"nodeValue", 3, PUT, XNodeVtbl, put_nodeValue, "v>-"),
    ROW(L"nodeType", 4, GET, XNodeVtbl, get_nodeType, ">L"),
    ROW(L"parentNode", 6, GET, XNodeVtbl, get_parentNode, ">N"),
    ROW(L"childNodes", 7, GET, XNodeVtbl, get_childNodes, ">N"),
    ROW(L"firstChild", 8, GET, XNodeVtbl, get_firstChild, ">N"),
    ROW(L"lastChild", 9, GET, XNodeVtbl, get_lastChild, ">N"),
    ROW(L"previousSibling", 10, GET, XNodeVtbl, get_previousSibling, ">N"),
    ROW(L"nextSibling", 11, GET, XNodeVtbl, get_nextSibling, ">N"),
    ROW(L"attributes", 12, GET, XNodeVtbl, get_attributes, ">N"),
    ROW(L"insertBefore", 13, CALL, XNodeVtbl, insertBefore, "nw>N"),
    ROW(L"replaceChild", 14, CALL, XNodeVtbl, replaceChild, "nn>N"),
    ROW(L"removeChild", 15, CALL, XNodeVtbl, removeChild, "n>N"),
    ROW(L"appendChild", 16, CALL, XNodeVtbl, appendChild, "n>N"),
    ROW(L"hasChildNodes", 17, CALL, XNodeVtbl, hasChildNodes, ">F"),
    ROW(L"ownerDocument", 18, GET, XNodeVtbl, get_ownerDocument, ">N"),
    ROW(L"cloneNode", 19, CALL, XNodeVtbl, cloneNode, "f>N"),
    ROW(L"nodeTypeString", 21, GET, XNodeVtbl, get_nodeTypeString, ">B"),
    ROW(L"text", 24, GET, XNodeVtbl, get_text, ">B"),
    ROW(L"text", 24, PUT, XNodeVtbl, put_text, "b>-"),
    ROW(L"specified", 22, GET, XNodeVtbl, get_specified, ">F"),
    ROW(L"definition", 23, GET, XNodeVtbl, get_definition, ">N"),
    ROW(L"nodeTypedValue", 25, GET, XNodeVtbl, get_nodeTypedValue, ">V"),
    ROW(L"nodeTypedValue", 25, PUT, XNodeVtbl, put_nodeTypedValue, "v>-"),
    ROW(L"dataType", 26, GET, XNodeVtbl, get_dataType, ">V"),
    ROW(L"dataType", 26, PUT, XNodeVtbl, put_dataType, "b>-"),
    ROW(L"xml", 27, GET, XNodeVtbl, get_xml, ">B"),
    ROW(L"transformNode", 28, CALL, XNodeVtbl, transformNode, "x>B"),
    ROW(L"selectNodes", 29, CALL, XNodeVtbl, selectNodes, "b>N"),
    ROW(L"selectSingleNode", 30, CALL, XNodeVtbl, selectSingleNode, "b>N"),
    ROW(L"parsed", 31, GET, XNodeVtbl, get_parsed, ">F"),
    ROW(L"namespaceURI", 32, GET, XNodeVtbl, get_namespaceURI, ">B"),
    ROW(L"prefix", 33, GET, XNodeVtbl, get_prefix, ">B"),
    ROW(L"baseName", 34, GET, XNodeVtbl, get_baseName, ">B"),
    ROW(L"transformNodeToObject", 35, CALL | 0x8000, XNodeVtbl, transformNodeToObject, "xv>-"),
};

static const XDisp element_rows[] = {
    ROW(L"tagName", 0x61, GET, XElementVtbl, get_tagName, ">B"),
    ROW(L"getAttribute", 0x63, CALL, XElementVtbl, getAttribute, "b>V"),
    ROW(L"setAttribute", 0x64, CALL, XElementVtbl, setAttribute, "bv>-"),
    ROW(L"removeAttribute", 0x65, CALL, XElementVtbl, removeAttribute, "b>-"),
    ROW(L"getAttributeNode", 0x66, CALL, XElementVtbl, getAttributeNode, "b>N"),
    ROW(L"setAttributeNode", 0x67, CALL, XElementVtbl, setAttributeNode, "n>N"),
    ROW(L"removeAttributeNode", 0x68, CALL, XElementVtbl, removeAttributeNode, "n>N"),
    ROW(L"getElementsByTagName", 0x69, CALL, XElementVtbl, getElementsByTagName, "b>N"),
    ROW(L"normalize", 0x6a, CALL, XElementVtbl, normalize, ">-"),
};

static const XDisp attribute_rows[] = {
    ROW(L"name", 0x76, GET, XAttributeVtbl, get_name, ">B"),
    ROW(L"value", 0x78, GET, XAttributeVtbl, get_value, ">V"),
    ROW(L"value", 0x78, PUT, XAttributeVtbl, put_value, "v>-"),
};

static const XDisp text_rows[] = {
    ROW(L"data", 0x6d, GET, XTextVtbl, get_data, ">B"),
    ROW(L"data", 0x6d, PUT, XTextVtbl, put_data, "b>-"),
    ROW(L"length", 0x6e, GET, XTextVtbl, get_length, ">L"),
    ROW(L"substringData", 0x6f, CALL, XTextVtbl, substringData, "ll>B"),
    ROW(L"appendData", 0x70, CALL, XTextVtbl, appendData, "b>-"),
    ROW(L"insertData", 0x71, CALL, XTextVtbl, insertData, "lb>-"),
    ROW(L"deleteData", 0x72, CALL, XTextVtbl, deleteData, "ll>-"),
    ROW(L"replaceData", 0x73, CALL, XTextVtbl, replaceData, "llb>-"),
    ROW(L"splitText", 0x7b, CALL, XTextVtbl, splitText, "l>N"),
};

/* comments get the text rows minus the last one, splitText. */
#define COMMENT_ROWS (sizeof(text_rows) / sizeof(text_rows[0]) - 1)

static const XDisp pi_rows[] = {
    ROW(L"target", 0x7f, GET, XPIVtbl, get_target, ">B"),
    ROW(L"data", 0x80, GET, XPIVtbl, get_data, ">B"),
    ROW(L"data", 0x80, PUT, XPIVtbl, put_data, "b>-"),
};

static const XDisp doctype_rows[] = {
    ROW(L"name", 0x83, GET, XDocTypeVtbl, get_name, ">B"),
    ROW(L"entities", 0x84, GET, XDocTypeVtbl, get_entities, ">N"),
    ROW(L"notations", 0x85, GET, XDocTypeVtbl, get_notations, ">N"),
};

static const XDisp document_rows[] = {
    ROW(L"doctype", 0x26, GET, XDocumentVtbl, get_doctype, ">N"),
    ROW(L"implementation", 0x27, GET, XDocumentVtbl, get_implementation, ">N"),
    ROW(L"documentElement", 0x28, GET, XDocumentVtbl, get_documentElement, ">N"),
    ROW(L"documentElement", 0x28, PUT, XDocumentVtbl, putref_documentElement, "n>-"),
    ROW(L"createElement", 0x29, CALL, XDocumentVtbl, createElement, "b>N"),
    ROW(L"createDocumentFragment", 0x2a, CALL, XDocumentVtbl, createDocumentFragment, ">N"),
    ROW(L"createTextNode", 0x2b, CALL, XDocumentVtbl, createTextNode, "b>N"),
    ROW(L"createComment", 0x2c, CALL, XDocumentVtbl, createComment, "b>N"),
    ROW(L"createCDATASection", 0x2d, CALL, XDocumentVtbl, createCDATASection, "b>N"),
    ROW(L"createProcessingInstruction", 0x2e, CALL, XDocumentVtbl, createProcessingInstruction, "bb>N"),
    ROW(L"createAttribute", 0x2f, CALL, XDocumentVtbl, createAttribute, "b>N"),
    ROW(L"createEntityReference", 0x31, CALL, XDocumentVtbl, createEntityReference, "b>N"),
    ROW(L"getElementsByTagName", 0x32, CALL, XDocumentVtbl, getElementsByTagName, "b>N"),
    ROW(L"createNode", 0x36, CALL, XDocumentVtbl, createNode, "vbb>N"),
    ROW(L"nodeFromID", 0x38, CALL, XDocumentVtbl, nodeFromID, "b>N"),
    ROW(L"load", 0x3a, CALL, XDocumentVtbl, load, "v>F"),
    ROW(L"readyState", DISPID_READYSTATE_ID, GET, XDocumentVtbl, get_readyState, ">L"),
    ROW(L"parseError", 0x3b, GET, XDocumentVtbl, get_parseError, ">N"),
    ROW(L"url", 0x3c, GET, XDocumentVtbl, get_url, ">B"),
    ROW(L"async", 0x3d, GET, XDocumentVtbl, get_async, ">F"),
    ROW(L"async", 0x3d, PUT, XDocumentVtbl, put_async, "f>-"),
    ROW(L"abort", 0x3e, CALL, XDocumentVtbl, abort, ">-"),
    ROW(L"loadXML", 0x3f, CALL, XDocumentVtbl, loadXML, "b>F"),
    ROW(L"save", 0x40, CALL, XDocumentVtbl, save, "v>-"),
    ROW(L"validateOnParse", 0x41, GET, XDocumentVtbl, get_validateOnParse, ">F"),
    ROW(L"validateOnParse", 0x41, PUT, XDocumentVtbl, put_validateOnParse, "f>-"),
    ROW(L"resolveExternals", 0x42, GET, XDocumentVtbl, get_resolveExternals, ">F"),
    ROW(L"resolveExternals", 0x42, PUT, XDocumentVtbl, put_resolveExternals, "f>-"),
    ROW(L"preserveWhiteSpace", 0x43, GET, XDocumentVtbl, get_preserveWhiteSpace, ">F"),
    ROW(L"preserveWhiteSpace", 0x43, PUT, XDocumentVtbl, put_preserveWhiteSpace, "f>-"),
    ROW(L"onreadystatechange", 0x44, PUT, XDocumentVtbl, put_onreadystatechange, "v>-"),
    ROW(L"ondataavailable", 0x45, PUT, XDocumentVtbl, put_ondataavailable, "v>-"),
    ROW(L"ontransformnode", 0x46, PUT, XDocumentVtbl, put_ontransformnode, "v>-"),
    ROW(L"namespaces", 0xc8, GET, XDocumentVtbl, get_namespaces, ">N"),
    ROW(L"schemas", 0xc9, GET, XDocumentVtbl, get_schemas, ">V"),
    ROW(L"schemas", 0xc9, PUT, XDocumentVtbl, putref_schemas, "v>-"),
    ROW(L"validate", 0xca, CALL, XDocumentVtbl, validate, ">N"),
    ROW(L"setProperty", 0xcb, CALL, XDocumentVtbl, setProperty, "bv>-"),
    ROW(L"getProperty", 0xcc, CALL, XDocumentVtbl, getProperty, "b>V"),
    ROW(L"validateNode", 0xcf, CALL, XDocumentVtbl, validateNode, "n>N"),
    ROW(L"importNode", 0xd0, CALL, XDocumentVtbl, importNode, "nf>N"),
};

static const XDisp list_rows[] = {
    ROW(L"item", 0, GET, XListVtbl, get_item, "l>N"),
    ROW(L"length", 0x4a, GET, XListVtbl, get_length, ">L"),
    ROW(L"nextNode", 0x4c, CALL, XListVtbl, nextNode, ">N"),
    ROW(L"reset", 0x4d, CALL, XListVtbl, reset, ">-"),
    ROW(L"_newEnum", DISPID_NEWENUM_ID, GET, XListVtbl, get__newEnum, ">U"),
};

static const XDisp selection_rows[] = {
    ROW(L"item", 0, GET, XSelectionVtbl, get_item, "l>N"),
    ROW(L"length", 0x4a, GET, XSelectionVtbl, get_length, ">L"),
    ROW(L"nextNode", 0x4c, CALL, XSelectionVtbl, nextNode, ">N"),
    ROW(L"reset", 0x4d, CALL, XSelectionVtbl, reset, ">-"),
    ROW(L"_newEnum", DISPID_NEWENUM_ID, GET, XSelectionVtbl, get__newEnum, ">U"),
    ROW(L"expr", 0xb, GET, XSelectionVtbl, get_expr, ">B"),
    ROW(L"expr", 0xb, PUT, XSelectionVtbl, put_expr, "b>-"),
    ROW(L"context", 0xc, GET, XSelectionVtbl, get_context, ">N"),
    ROW(L"context", 0xc, PUT, XSelectionVtbl, putref_context, "n>-"),
    ROW(L"peekNode", 0xd, CALL, XSelectionVtbl, peekNode, ">N"),
    ROW(L"matches", 0xe, CALL, XSelectionVtbl, matches, "n>N"),
    ROW(L"removeNext", 0xf, CALL, XSelectionVtbl, removeNext, ">N"),
    ROW(L"removeAll", 0x10, CALL, XSelectionVtbl, removeAll, ">-"),
    ROW(L"clone", 0x11, CALL, XSelectionVtbl, clone, ">N"),
    ROW(L"getProperty", 0x12, CALL, XSelectionVtbl, getProperty, "b>V"),
    ROW(L"setProperty", 0x13, CALL, XSelectionVtbl, setProperty, "bv>-"),
};

static const XDisp map_rows[] = {
    ROW(L"item", 0, GET, XMapVtbl, get_item, "l>N"),
    ROW(L"length", 0x4a, GET, XMapVtbl, get_length, ">L"),
    ROW(L"getNamedItem", 0x53, CALL, XMapVtbl, getNamedItem, "b>N"),
    ROW(L"setNamedItem", 0x54, CALL, XMapVtbl, setNamedItem, "n>N"),
    ROW(L"removeNamedItem", 0x55, CALL, XMapVtbl, removeNamedItem, "b>N"),
    ROW(L"getQualifiedItem", 0x57, CALL, XMapVtbl, getQualifiedItem, "bb>N"),
    ROW(L"removeQualifiedItem", 0x58, CALL, XMapVtbl, removeQualifiedItem, "bb>N"),
    ROW(L"nextNode", 0x59, CALL, XMapVtbl, nextNode, ">N"),
    ROW(L"reset", 0x5a, CALL, XMapVtbl, reset, ">-"),
    ROW(L"_newEnum", DISPID_NEWENUM_ID, GET, XMapVtbl, get__newEnum, ">U"),
};

static const XDisp error_rows[] = {
    ROW(L"errorCode", 0, GET, XErrVtbl, get_errorCode, ">L"),
    ROW(L"url", 0xb2, GET, XErrVtbl, get_url, ">B"),
    ROW(L"reason", 0xb3, GET, XErrVtbl, get_reason, ">B"),
    ROW(L"srcText", 0xb4, GET, XErrVtbl, get_srcText, ">B"),
    ROW(L"line", 0xb5, GET, XErrVtbl, get_line, ">L"),
    ROW(L"linepos", 0xb6, GET, XErrVtbl, get_linepos, ">L"),
    ROW(L"filepos", 0xb7, GET, XErrVtbl, get_filepos, ">L"),
    ROW(L"errorXPath", 0xbd, GET, XErrVtbl, get_errorXPath, ">B"),
    ROW(L"allErrors", 0xba, GET, XErrVtbl, get_allErrors, ">N"),
    ROW(L"errorParameters", 0xbb, CALL, XErrVtbl, errorParameters, "l>B"),
    ROW(L"errorParametersCount", 0xbc, GET, XErrVtbl, get_errorParametersCount, ">L"),
};

static const XDisp impl_rows[] = {
    ROW(L"hasFeature", 0x91, CALL, XImplVtbl, hasFeature, "bb>F"),
};

static const XDisp template_rows[] = {
    ROW(L"stylesheet", 2, DISPATCH_PROPERTYGET, XTplVtbl, get_stylesheet, ">N"),
    ROW(L"stylesheet", 2, PUT, XTplVtbl, putref_stylesheet, "n>-"),
    ROW(L"createProcessor", 3, CALL, XTplVtbl, createProcessor, ">N"),
};

static const XDisp processor_rows[] = {
    ROW(L"input", 2, DISPATCH_PROPERTYGET, XProcVtbl, get_input, ">V"),
    ROW(L"input", 2, PUT, XProcVtbl, put_input, "v>-"),
    ROW(L"ownerTemplate", 3, DISPATCH_PROPERTYGET, XProcVtbl, get_ownerTemplate, ">N"),
    ROW(L"setStartMode", 4, CALL, XProcVtbl, setStartMode, "st>-"),
    ROW(L"startMode", 5, DISPATCH_PROPERTYGET, XProcVtbl, get_startMode, ">B"),
    ROW(L"startModeURI", 6, DISPATCH_PROPERTYGET, XProcVtbl, get_startModeURI, ">B"),
    ROW(L"output", 7, DISPATCH_PROPERTYGET, XProcVtbl, get_output, ">V"),
    ROW(L"output", 7, PUT, XProcVtbl, put_output, "v>-"),
    ROW(L"transform", 8, CALL, XProcVtbl, transform, ">F"),
    ROW(L"reset", 9, CALL, XProcVtbl, reset, ">-"),
    ROW(L"readyState", 10, DISPATCH_PROPERTYGET, XProcVtbl, get_readyState, ">L"),
    ROW(L"addParameter", 11, CALL, XProcVtbl, addParameter, "bvc>-"),
    ROW(L"addObject", 12, CALL, XProcVtbl, addObject, "ob>-"),
    ROW(L"stylesheet", 13, DISPATCH_PROPERTYGET, XProcVtbl, get_stylesheet, ">N"),
};

#define COUNT(rows) (sizeof(rows) / sizeof((rows)[0]))

#define SET1(a) {{a}, {COUNT(a)}}
#define SET2(a, b) {{a, b}, {COUNT(a), COUNT(b)}}

static const XDispSet sets[XN_TYPES] = {
    [0] = SET1(node_rows),
    [XN_ELEMENT] = SET2(element_rows, node_rows),
    [XN_ATTRIBUTE] = SET2(attribute_rows, node_rows),
    [XN_TEXT] = SET2(text_rows, node_rows),
    [XN_CDATA] = SET2(text_rows, node_rows),
    [XN_ENTITYREF] = SET1(node_rows),
    [XN_ENTITY] = SET1(node_rows),
    [XN_PI] = SET2(pi_rows, node_rows),
    [XN_COMMENT] = {{text_rows, node_rows}, {COMMENT_ROWS, COUNT(node_rows)}},
    [XN_DOCUMENT] = SET2(document_rows, node_rows),
    [XN_DOCTYPE] = SET2(doctype_rows, node_rows),
    [XN_FRAGMENT] = SET1(node_rows),
    [XN_NOTATION] = SET1(node_rows),
};

const XDispSet *xd_dispset_for_node(UINT type)
{
    return &sets[type < XN_TYPES ? type : 0];
}

const XDispSet xd_dispset_list = SET1(list_rows);
const XDispSet xd_dispset_selection = SET1(selection_rows);
const XDispSet xd_dispset_map = SET1(map_rows);
const XDispSet xd_dispset_error = SET1(error_rows);
const XDispSet xd_dispset_impl = SET1(impl_rows);
const XDispSet xd_dispset_template = SET1(template_rows);
const XDispSet xd_dispset_processor = SET1(processor_rows);

/* ------------------------------------------------------------------ names */

HRESULT xd_disp_names(const XDispSet *set, REFIID riid, LPOLESTR *names, UINT cnames, LCID lcid, DISPID *ids)
{
    if (!names || !ids || !cnames)
        return E_INVALIDARG;
    if (!IsEqualIID(riid, &IID_NULL))
        return (HRESULT)0x80020001L; /* DISP_E_UNKNOWNINTERFACE. */
    for (UINT i = 0; i < cnames; i++)
        ids[i] = DISPID_UNKNOWN;
    for (UINT t = 0; t < 3 && set->t[t]; t++) {
        for (UINT i = 0; i < set->n[t]; i++) {
            if (names[0] && !xd_wcsicmp(names[0], set->t[t][i].name)) {
                ids[0] = set->t[t][i].id;
                return cnames == 1 ? S_OK : DISP_E_UNKNOWNNAME;
            }
        }
    }
    return DISP_E_UNKNOWNNAME;
}

/* ------------------------------------------------------------------ calls */

typedef UINT_PTR W;

#define MAX_WORDS 12

static HRESULT call_words(void *fn, W *w, UINT n)
{
#define P(i) w[i]
    switch (n) {
    case 1:
        return ((HRESULT(WINAPI *)(W))fn)(P(0));
    case 2:
        return ((HRESULT(WINAPI *)(W, W))fn)(P(0), P(1));
    case 3:
        return ((HRESULT(WINAPI *)(W, W, W))fn)(P(0), P(1), P(2));
    case 4:
        return ((HRESULT(WINAPI *)(W, W, W, W))fn)(P(0), P(1), P(2), P(3));
    case 5:
        return ((HRESULT(WINAPI *)(W, W, W, W, W))fn)(P(0), P(1), P(2), P(3), P(4));
    case 6:
        return ((HRESULT(WINAPI *)(W, W, W, W, W, W))fn)(P(0), P(1), P(2), P(3), P(4), P(5));
    case 7:
        return ((HRESULT(WINAPI *)(W, W, W, W, W, W, W))fn)(P(0), P(1), P(2), P(3), P(4), P(5), P(6));
    case 8:
        return ((HRESULT(WINAPI *)(W, W, W, W, W, W, W, W))fn)(P(0), P(1), P(2), P(3), P(4), P(5), P(6), P(7));
    case 9:
        return ((HRESULT(WINAPI *)(W, W, W, W, W, W, W, W, W))fn)(P(0), P(1), P(2), P(3), P(4), P(5), P(6), P(7),
                                                                    P(8));
    case 10:
        return ((HRESULT(WINAPI *)(W, W, W, W, W, W, W, W, W, W))fn)(P(0), P(1), P(2), P(3), P(4), P(5), P(6), P(7),
                                                                       P(8), P(9));
    case 11:
        return ((HRESULT(WINAPI *)(W, W, W, W, W, W, W, W, W, W, W))fn)(P(0), P(1), P(2), P(3), P(4), P(5), P(6),
                                                                          P(7), P(8), P(9), P(10));
    default:
        return ((HRESULT(WINAPI *)(W, W, W, W, W, W, W, W, W, W, W, W))fn)(P(0), P(1), P(2), P(3), P(4), P(5), P(6),
                                                                              P(7), P(8), P(9), P(10), P(11));
    }
#undef P
}

static void exception_info(EXCEPINFO *excep, HRESULT hr)
{
    if (!excep)
        return;
    memset(excep, 0, sizeof(*excep));
    excep->scode = hr;
    excep->bstrSource = SysAllocString(L"msxml");
    WCHAR *taken = xd_take_error_text(hr);
    const WCHAR *text = taken ? taken : xd_message((UINT)hr);
    if (!text) {
        switch ((UINT)hr) {
        case 0x80070057u:
            text = L"The parameter is incorrect.\r\n";
            break;
        case 0x80020005u:
            text = L"Type mismatch.\r\n";
            break;
        case 0x8007007Bu:
            text = L"The filename, directory name, or volume label syntax is incorrect.\r\n";
            break;
        }
        if (text)
            taken = xd_wcsdup(text, xd_wlen(text));
    }
    if (text) {
        UINT len = xd_wlen(text);
        while (!taken && len && (text[len - 1] == '\r' || text[len - 1] == '\n'))
            len--;
        excep->bstrDescription = SysAllocStringLen(text, len);
    }
    xd_free(taken);
}

HRESULT xd_disp_invoke(void *self, const XDispSet *set, DISPID id, REFIID riid, LCID lcid, WORD flags,
                       DISPPARAMS *params, VARIANT *result, EXCEPINFO *excep, UINT *argerr)
{
    const XDisp *row = nullptr;
    BOOL put = (flags & PUT) != 0;
    for (UINT t = 0; t < 3 && set->t[t] && !row; t++) {
        for (UINT i = 0; i < set->n[t]; i++) {
            const XDisp *r = &set->t[t][i];
            if (r->id == id && (put ? (r->flags & PUT) : (r->flags & (DISPATCH_METHOD | DISPATCH_PROPERTYGET)))) {
                row = r;
                break;
            }
        }
    }
    if (!row)
        return DISP_E_MEMBERNOTFOUND;
    if (!IsEqualIID(riid, &IID_NULL))
        return (HRESULT)0x80020001L;

    UINT supplied = params ? params->cArgs : 0;
    UINT required = 0, total = 0;
    const char *sig = row->sig;
    for (; sig[total] && sig[total] != '>'; total++)
        if (sig[total] != 'w' && sig[total] != 'c' && sig[total] != 't')
            required++;
    char out_kind = sig[total] == '>' ? sig[total + 1] : '-';
    if (supplied < required || supplied > total)
        return DISP_E_BADPARAMCOUNT;

    W words[MAX_WORDS];
    UINT nw = 0;
    words[nw++] = (W)self;

    VARIANT vcopy[3];
    BOOL owned[3] = {};
    UINT nv = 0;
    BSTR temps[3];
    UINT ntemps = 0;
    XNode *nodes[3];
    UINT nnodes = 0;
    HRESULT hr = S_OK;

    for (UINT i = 0; i < total && SUCCEEDED(hr); i++) {
        const VARIANT *arg = i < supplied ? &params->rgvarg[supplied - 1 - i] : nullptr;
        char kind = sig[i];
        if (!arg && kind != 'w' && kind != 'c' && kind != 't') {
            hr = DISP_E_BADPARAMCOUNT;
            break;
        }
        switch (kind) {
        case 'o': {
            const VARIANT *v = xv_deref(arg);
            void *p = nullptr;
            if (v->vt == VT_DISPATCH || v->vt == VT_UNKNOWN)
                p = v->punkVal;
            else if ((v->vt == (VT_DISPATCH | VT_BYREF) || v->vt == (VT_UNKNOWN | VT_BYREF)) && v->ppunkVal)
                p = *v->ppunkVal;
            else if (!xv_is_missing(v)) {
                hr = DISP_E_TYPEMISMATCH;
                break;
            }
            words[nw++] = (W)p;
            break;
        }
        case 's':
        case 't':
        case 'c':
        case 'b': {
            if (!arg) {
                words[nw++] = 0;
                break;
            }
            if ((kind == 's' || kind == 't') && xv_deref(arg)->vt == VT_NULL) {
                hr = DISP_E_TYPEMISMATCH;
                break;
            }
            const VARIANT *v = xv_deref(arg);
            BSTR s = nullptr;
            if ((v->vt & ~VT_BYREF) == VT_BSTR) {
                s = (v->vt & VT_BYREF) ? (v->pbstrVal ? *v->pbstrVal : nullptr) : v->bstrVal;
            } else if (!xv_is_missing(v)) {
                XBuf b = {};
                if (!xv_to_string(v, &b)) {
                    xb_free(&b);
                    hr = DISP_E_TYPEMISMATCH;
                    break;
                }
                s = SysAllocStringLen(b.s ? b.s : L"", b.length);
                xb_free(&b);
                if (!s) {
                    hr = E_OUTOFMEMORY;
                    break;
                }
                temps[ntemps++] = s;
            }
            words[nw++] = (W)s;
            break;
        }
        case 'l': {
            LONG n;
            if (!xv_to_long(arg, &n)) {
                hr = DISP_E_TYPEMISMATCH;
                break;
            }
            words[nw++] = (W)(UINT_PTR)(ULONG)n;
            break;
        }
        case 'f': {
            BOOL on;
            if (!xv_to_bool(arg, &on)) {
                hr = DISP_E_TYPEMISMATCH;
                break;
            }
            words[nw++] = on ? 0xFFFF : 0;
            break;
        }
        case 'x':
            if (xv_is_missing(arg) && arg->vt != VT_ERROR) {
                hr = DISP_E_TYPEMISMATCH;
                break;
            }
            [[fallthrough]];
        case 'n': {
            XNode *node = nullptr;
            if (!xv_is_missing(arg)) {
                node = xv_to_node(arg);
                if (!node) {
                    hr = DISP_E_TYPEMISMATCH;
                    break;
                }
                nodes[nnodes++] = node;
            }
            words[nw++] = (W)node;
            break;
        }
        case 'v':
        case 'w': {
            VARIANT *c = &vcopy[nv];
            VariantInit(c);
            const VARIANT *v = xv_deref(arg);
            if (v) {
                if (v->vt & VT_BYREF) {
                    /* a byref value is read through. */
                    if (FAILED(VariantCopyInd(c, v))) {
                        hr = DISP_E_TYPEMISMATCH;
                        break;
                    }
                    owned[nv] = TRUE;
                } else {
                    /* a shallow copy, the caller still owns what it holds. */
                    *c = *v;
                }
            }
            nv++;
#if INTPTR_MAX == INT32_MAX
            for (UINT k = 0; k < sizeof(VARIANT) / sizeof(W); k++)
                words[nw++] = ((W *)c)[k];
#else
            words[nw++] = (W)c;
#endif
            break;
        }
        }
    }

    BOOL called = FALSE;
    BSTR bout = nullptr;
    LONG lout = 0;
    VARIANT_BOOL fout = 0;
    VARIANT vout;
    VariantInit(&vout);
    void *pout = nullptr;
    if (SUCCEEDED(hr)) {
        switch (out_kind) {
        case 'B':
            words[nw++] = (W)&bout;
            break;
        case 'L':
            words[nw++] = (W)&lout;
            break;
        case 'F':
            words[nw++] = (W)&fout;
            break;
        case 'V':
            words[nw++] = (W)&vout;
            break;
        case 'N':
        case 'U':
            words[nw++] = (W)&pout;
            break;
        }
        void *fn = ((void **)*(void ***)self)[row->slot];
        hr = call_words(fn, words, nw);
        called = TRUE;
    } else if (hr == DISP_E_TYPEMISMATCH && argerr) {
        *argerr = 0;
    }

    for (UINT i = 0; i < ntemps; i++)
        SysFreeString(temps[i]);
    for (UINT i = 0; i < nnodes; i++)
        xn_release(nodes[i]);
    for (UINT i = 0; i < nv; i++)
        if (owned[i])
            VariantClear(&vcopy[i]);

    if (FAILED(hr)) {
        SysFreeString(bout);
        VariantClear(&vout);
        if (pout)
            ((IUnknown *)pout)->lpVtbl->Release((IUnknown *)pout);
        if (row->flags & 0x8000)
            return hr;
        if (hr == DISP_E_BADVARTYPE) {
            if (result)
                *result = vout;
            return hr;
        }
        if (!called && (hr == DISP_E_TYPEMISMATCH || hr == DISP_E_BADPARAMCOUNT || hr == E_OUTOFMEMORY))
            return hr;
        exception_info(excep, hr);
        return DISP_E_EXCEPTION;
    }

    VARIANT *dest = result;
    VARIANT scratch;
    if (!dest) {
        VariantInit(&scratch);
        dest = &scratch;
    } else {
        VariantInit(dest);
    }
    switch (out_kind) {
    case 'B':
        dest->vt = VT_BSTR;
        dest->bstrVal = bout;
        break;
    case 'L':
        dest->vt = VT_I4;
        dest->lVal = lout;
        break;
    case 'F':
        dest->vt = VT_BOOL;
        dest->boolVal = fout;
        break;
    case 'V':
        *dest = vout;
        break;
    case 'N':
        dest->vt = VT_DISPATCH;
        dest->pdispVal = (IDispatch *)pout;
        break;
    case 'U':
        dest->vt = VT_UNKNOWN;
        dest->punkVal = (IUnknown *)pout;
        break;
    }
    if (!result)
        VariantClear(&scratch);
    return hr;
}
