/*
 * the dom engine of msxml3.dll and msxml6.dll: nodes that are their own com objects, an xml parser, a writer and
 * an xpath 1.0 evaluator.
 */
#ifndef MIME_XMLDOM_H
#define MIME_XMLDOM_H

#include <mime/win32/oleauto.h>
#include <mime/win32/olectl.h>
#include <mime/win32/objidl.h>
#include <mime/win32/winnls.h>

#ifndef DISP_E_MEMBERNOTFOUND
#define DISP_E_MEMBERNOTFOUND ((HRESULT)0x80020003L)
#endif
#ifndef DISP_E_TYPEMISMATCH
#define DISP_E_TYPEMISMATCH ((HRESULT)0x80020005L)
#endif
#ifndef DISP_E_UNKNOWNNAME
#define DISP_E_UNKNOWNNAME ((HRESULT)0x80020006L)
#endif
#ifndef DISP_E_EXCEPTION
#define DISP_E_EXCEPTION ((HRESULT)0x80020009L)
#endif
#ifndef DISP_E_BADPARAMCOUNT
#define DISP_E_BADPARAMCOUNT ((HRESULT)0x8002000EL)
#endif
#ifndef DISP_E_PARAMNOTFOUND
#define DISP_E_PARAMNOTFOUND ((HRESULT)0x80020004L)
#endif
#ifndef CLASS_E_NOAGGREGATION
#define CLASS_E_NOAGGREGATION ((HRESULT)0x80040110L)
#endif
#ifndef CLASS_E_CLASSNOTAVAILABLE
#define CLASS_E_CLASSNOTAVAILABLE ((HRESULT)0x80040111L)
#endif

/* dom error codes, which are also the message table ids. */
#define XE_OPEN_FILE 0xC00CE001u
#define XE_UNDEFINED_ENTITY 0xC00CE002u
#define XE_ENTITY_LOOP 0xC00CE003u
#define XE_UNDECLARED_PREFIX 0xC00CE01Du
#define XE_EMPTY_URI 0xC00CE027u
#define XE_MISSING_EQUALS 0xC00CE501u
#define XE_NO_QUOTE 0xC00CE502u
#define XE_BAD_COMMENT 0xC00CE503u
#define XE_NAME_START 0xC00CE504u
#define XE_NAME_CHAR 0xC00CE505u
#define XE_LT_IN_ATTRIBUTE 0xC00CE506u
#define XE_XML_DECL_SYNTAX 0xC00CE507u
#define XE_TEXT_CHAR 0xC00CE508u
#define XE_NEED_SPACE 0xC00CE509u
#define XE_NEED_GT 0xC00CE50Au
#define XE_DTD_CHAR 0xC00CE50Cu
#define XE_NEED_SEMICOLON 0xC00CE50Du
#define XE_ENTITY_CHAR 0xC00CE50Eu
#define XE_NEED_BRACKET 0xC00CE510u
#define XE_CDATA_SYNTAX 0xC00CE511u
#define XE_PI_SYNTAX 0xC00CE51Au
#define XE_MULTIPLE_COLONS 0xC00CE51Cu
#define XE_DECIMAL_DIGIT 0xC00CE51Du
#define XE_HEX_DIGIT 0xC00CE51Eu
#define XE_BAD_UNICODE 0xC00CE51Fu
#define XE_END_TAG_UNEXPECTED 0xC00CE552u
#define XE_TAGS_NOT_CLOSED 0xC00CE553u
#define XE_DUPLICATE_ATTRIBUTE 0xC00CE554u
#define XE_TWO_ROOTS 0xC00CE555u
#define XE_TOP_LEVEL 0xC00CE556u
#define XE_BAD_XML_DECL 0xC00CE557u
#define XE_NO_ROOT 0xC00CE558u
#define XE_EOF 0xC00CE559u
#define XE_CDATA_END_IN_TEXT 0xC00CE55Cu
#define XE_PI_NOT_CLOSED 0xC00CE55Du
#define XE_ELEMENT_NOT_CLOSED 0xC00CE55Eu
#define XE_END_TAG_NO_GT 0xC00CE55Fu
#define XE_STRING_NOT_CLOSED 0xC00CE560u
#define XE_COMMENT_NOT_CLOSED 0xC00CE561u
#define XE_DECL_NOT_CLOSED 0xC00CE562u
#define XE_CDATA_NOT_CLOSED 0xC00CE564u
#define XE_DECL_NAME 0xC00CE565u
#define XE_EXTERNAL_ID 0xC00CE566u
#define XE_RESERVED_PREFIX 0xC00CE568u
#define XE_VERSION_REQUIRED 0xC00CE569u
#define XE_END_TAG_MISMATCH 0xC00CE56Du
#define XE_BAD_ENCODING 0xC00CE56Eu
#define XE_SWITCH_ENCODING 0xC00CE56Fu
#define XE_STANDALONE 0xC00CE579u
#define XE_DOCTYPE_OUTSIDE_PROLOG 0xC00CE57Eu
#define XE_BAD_VERSION 0xC00CE57Fu
#define XE_TWO_DOCTYPES 0xC00CE581u
#define XE_PROCESSING_RESOURCE 0xC00CE582u
#define XE_NAME_REQUIRED 0xC00CE583u
#define XE_DTD_PROHIBITED 0xC00CE584u
#define XE_MAX_SIZE 0xC00CE585u
#define XE_MAX_DEPTH 0xC00CE586u
#define XE_NODETEST 0xC00CE600u
#define XE_EXPR_EXPECTED 0xC00CE606u
#define XE_BAD_AXIS 0xC00CE607u
#define XE_BAD_ARGUMENTS 0xC00CE60Au
#define XE_XPATH_INCOMPLETE 0xC00CE380u
#define XE_XPATH_UNEXPECTED 0xC00CE381u
#define XE_XPATH_CHAR 0xC00CE386u
#define XE_NOT_NODESET 0xC00CE617u
#define XE_NOT_DOM_NODE 0xC00CE382u
#define XE_NUMBER_EXPECTED 0xC00CE603u
#define XE_PATTERN_FUNCTION 0xC00CE338u
#define XE_NO_VARIABLES 0xC00CE605u
#define XE_PATTERN_VAR 0xC00CE610u
#define XE_VAR_UNRESOLVED 0xC00CE316u
#define XE_BAD_FUNCTION 0xC00CE334u
#define XE_INSERT_NOT_FOUND 0xC00CE206u
#define XE_NODE_NOT_FOUND 0xC00CE207u
#define XE_BAD_NODE_TYPE 0xC00CE208u
#define XE_UNDER_ITSELF 0xC00CE20Au
#define XE_INSERT_NOT_CHILD 0xC00CE20Bu
#define XE_NO_ATTRIBUTES 0xC00CE20Cu
#define XE_NOT_A_CHILD 0xC00CE20Du
#define XE_NEED_NAME 0xC00CE20Eu
#define XE_PARAMETER_MISSING 0xC00CE210u
#define XE_ATTRIBUTE_IN_USE 0xC00CE214u
#define XE_BAD_OFFSET 0xC00CE218u
#define XE_NOT_SPECIFIED_ATTRIBUTE 0xC00CE219u
#define XE_BAD_PROPERTY_NAME 0xC00CE21Du
#define XE_BAD_PROPERTY_VALUE 0xC00CE21Eu
#define XE_NO_SCHEMA 0xC00CE224u
#define XE_SELECTION_NAMESPACES 0xC00CE22Eu
#define XE_NAME_HAS_CHAR 0xC00CE22Fu
#define XE_XSLPATTERN_UNSUPPORTED 0xC00CE23Cu
#define XE_FILE_NOT_FOUND 0x800C0006u
#define XE_NAME_UNSUPPORTED 0xC00CE21Cu

typedef struct XDoc XDoc;
typedef struct XNode XNode;
typedef struct XList XList;

enum {
    XN_ELEMENT = 1,
    XN_ATTRIBUTE = 2,
    XN_TEXT = 3,
    XN_CDATA = 4,
    XN_ENTITYREF = 5,
    XN_ENTITY = 6,
    XN_PI = 7,
    XN_COMMENT = 8,
    XN_DOCUMENT = 9,
    XN_DOCTYPE = 10,
    XN_FRAGMENT = 11,
    XN_NOTATION = 12,
    XN_TYPES = 13,
};

#define XF_XMLDECL 0x01   /* the processing instruction is the xml declaration. */
#define XF_EXPLICIT 0x02  /* written with an end tag though empty, as parsed from a start and end tag or after its text was set to empty. */
#define XF_WS_INSIDE 0x04 /* the parser dropped white space between the start tag and the first child. */
#define XF_WS_AFTER 0x08  /* the parser dropped white space after the node. */
#define XF_DOE 0x10       /* a text node of a result tree that is written without escapes. */

/*
 * a node is its own com object: the first member is its type's vtable. refs counts com references, subs the
 * nodes of the subtree, itself included, that have any. an unreferenced subtree outside a document is freed, and a
 * document lives while any of its nodes is referenced.
 */
struct XNode {
    const void *vtbl;
    UINT8 type;
    UINT8 flags;
    UINT16 local; /* where the local name starts in qname, 0 for none. */
    LONG refs;
    LONG subs;
    XNode *parent;
    XNode *first;
    XNode *last;
    XNode *prev;
    XNode *next;
    XNode *attrs; /* first attribute, chained by prev and next. */
    XDoc *doc;
    const WCHAR *qname; /* an interned string. */
    const WCHAR *uri;   /* an interned string or null. */
    WCHAR *value;
    UINT length; /* of value. */
    UINT order;  /* position in the document, valid while doc->order_version is doc->version. */
};

typedef struct XErr {
    LONG code;
    WCHAR *reason;
    WCHAR *src;
    WCHAR *url;
    LONG line;
    LONG linepos;
    LONG filepos;
    UINT params;
    WCHAR *param[3];
} XErr;

#define XDF_ASYNC 0x0001
#define XDF_VALIDATE 0x0002
#define XDF_RESOLVE 0x0004
#define XDF_PRESERVE 0x0008
#define XDF_PROHIBIT_DTD 0x0010
#define XDF_XPATH 0x0020 /* the selection language is XPath, else XSLPattern. */
#define XDF_NEWPARSER 0x0040
#define XDF_SERVER_HTTP 0x0080
#define XDF_FORCED_RESYNC 0x0100
#define XDF_V6 0x0200      /* msxml6 behavior. */
#define XDF_FREE 0x0400    /* a free threaded document. */
#define XDF_ALLOW_DOCFN 0x0800
#define XDF_ALLOW_SCRIPT 0x1000
#define XDF_POPULATE 0x2000
#define XDF_NORMALIZE_ATTR 0x4000
#define XDF_MULTIPLE_ERRORS 0x8000
#define XDF_INLINE_SCHEMA 0x10000

typedef struct XAtom XAtom;
typedef struct XNs {
    WCHAR *prefix;
    WCHAR *uri;
} XNs;

typedef struct XEntity {
    const WCHAR *name;
    WCHAR *value;
    UINT length;
} XEntity;

struct XDoc {
    XNode node; /* the document node: must stay first. */
    LONG live;  /* referenced nodes, the document node included. */
    UINT seq;   /* the order documents were made in, for node sets that span several. */
    UINT flags;
    UINT version;       /* bumped on every tree change, live lists resync on it. */
    UINT order_version; /* the version node order was last numbered at. */
    LONG ready_state;
    LONG max_depth;
    LONG max_size;
    WCHAR *url;
    XErr err;
    XAtom **atoms;
    UINT atom_slots;
    UINT atom_count;
    void *arena; /* blocks that atoms and the like are carved from. */
    UINT8 *arena_cur;
    UINT8 *arena_end;
    XNs *sel_ns; /* SelectionNamespaces prefixes. */
    UINT sel_ns_count;
    WCHAR *sel_ns_text;
    XEntity *entities;
    UINT entity_count;
    UINT entity_cap;
    IDispatch *on_readystatechange;
    IDispatch *on_dataavailable;
    IDispatch *on_transformnode;
    VARIANT schemas;
    WCHAR *encoding; /* of the declaration or the input that was loaded. */
};

/* ------------------------------------------------------------------ strings and memory, util.c */

void *xd_alloc(SIZE_T size);
void *xd_alloc_raw(SIZE_T size);
void *xd_realloc(void *p, SIZE_T size);
void xd_free(void *p);
WCHAR *xd_wcsdup(const WCHAR *s, UINT length);
UINT xd_wlen(const WCHAR *s);
BSTR xd_bstr(const WCHAR *s, UINT length);

typedef struct XBuf {
    WCHAR *s;
    UINT length;
    UINT cap;
    BOOL failed;
} XBuf;
void xb_free(XBuf *b);
void xb_reserve(XBuf *b, UINT more);
void xb_put(XBuf *b, const WCHAR *s, UINT length);
void xb_putc(XBuf *b, WCHAR c);
void xb_puta(XBuf *b, const char *ascii);

/* interned per document: equal strings share one pointer. */
const WCHAR *xd_intern(XDoc *doc, const WCHAR *s, UINT length);
UINT xd_atom_len(const WCHAR *atom);
/* the interned copy if there is one, else null, and then no node can have that name. */
const WCHAR *xd_find_atom(XDoc *doc, const WCHAR *s, UINT length);
void *xd_arena(XDoc *doc, SIZE_T size);
void xd_atoms_free(XDoc *doc);

BOOL xd_is_name_start(UINT c);
BOOL xd_is_name_char(UINT c);
BOOL xd_is_space(WCHAR c);
BOOL xd_is_xml_char(UINT c);
/* index of the first bad character, or -1 for a valid name. */
int xd_check_name(const WCHAR *s, UINT length, BOOL allow_colon);
int xd_wcsicmp(const WCHAR *a, const WCHAR *b);

void xd_format_number(double value, XBuf *out);
/* the shortest plain decimal text that reads back as the same double. */
void xd_format_double(double value, XBuf *out);
/* the double nearest to 0.digits... times ten to the exp10 and the digits count, digits are characters. */
double xd_decimal_to_double(const WCHAR *digits, UINT count, int exp10);
double xd_parse_number(const WCHAR *s, UINT length);
double xd_nan(void);
BOOL xd_isnan(double value);

/* ------------------------------------------------------------------ the tree, dom.c */

extern LONG xd_objects; /* live module objects, for DllCanUnloadNow. */

void xd_init(void);

XDoc *xd_doc_new(UINT flags);
XNode *xd_node_new(XDoc *doc, UINT type, const WCHAR *qname, UINT qlen, const WCHAR *uri);
void xd_node_set_name(XNode *n, const WCHAR *qname, UINT qlen, const WCHAR *uri);
void xd_node_set_value(XNode *n, const WCHAR *value, UINT length);
BOOL xd_node_set_value_owned(XNode *n, WCHAR *value, UINT length);

ULONG xn_addref(XNode *n);
ULONG xn_release(XNode *n);
HRESULT xn_query(XNode *n, REFIID riid, void **out);
const void *xd_vtbl_for(UINT type);

/* tree surgery: the callers do the checks, these only link. */
void xd_link(XNode *parent, XNode *child, XNode *before);
void xd_unlink(XNode *child);
void xd_attr_link(XNode *element, XNode *attr);
void xd_attr_unlink(XNode *attr);
void xd_free_if_orphan(XNode *n);
void xd_touch(XDoc *doc);
/* whether the xml of a document is indented: msxml6 follows preserveWhiteSpace, msxml3 always does. */
BOOL xd_indent(const XDoc *doc);
void xd_clear_children(XNode *parent);

XNode *xd_attr_find(XNode *element, const WCHAR *qname, UINT length);
XNode *xd_attr_find_ns(XNode *element, const WCHAR *local, UINT length, const WCHAR *uri);
XNode *xd_child_at(XNode *parent, UINT index);
UINT xd_child_count(XNode *parent);
XNode *xd_document_element(XDoc *doc);
BOOL xd_is_ancestor(XNode *maybe, XNode *of);
const WCHAR *xd_local(const XNode *n);
UINT xd_local_length(const XNode *n);
BOOL xd_is_xmlns_attr(const XNode *n);
const WCHAR *xd_lookup_ns(XNode *at, const WCHAR *prefix, UINT length);

XNode *xd_clone(XNode *src, XDoc *into, BOOL deep);
BOOL xd_text(XNode *n, XBuf *out);
/* strings of nodes include the white space the parser dropped, as xslt sees them: returns the old mode. */
BOOL xd_text_mode(BOOL on);
void xd_text_xsl(XNode *n, XBuf *out);
BOOL xd_set_text(XNode *n, const WCHAR *text, UINT length);
void xd_normalize(XNode *n);
void xd_number_order(XDoc *doc);

/* child insert, replace and remove checks: a dom error code, or zero when allowed. */
HRESULT xd_check_insert(XNode *parent, XNode *child, XNode *before);
HRESULT xd_insert(XNode *parent, XNode *child, XNode *before, XNode **inserted);
HRESULT xd_remove(XNode *parent, XNode *child);

/* ------------------------------------------------------------------ errors, messages.c */

const WCHAR *xd_message(UINT id);
/* an allocated message with %1 and %2 filled in. */
WCHAR *xd_format(UINT id, const WCHAR *p1, const WCHAR *p2);
void xd_err_clear(XDoc *doc);
void xd_err_free(XErr *e);
void xd_err_set(XDoc *doc, UINT code, LONG line, LONG linepos, LONG filepos, const WCHAR *src, UINT srclen,
                const WCHAR *p1, const WCHAR *p2);
void xd_err_set_reason(XDoc *doc, UINT code, const WCHAR *reason, const WCHAR *url);
void xd_err_copy(XErr *to, const XErr *from);
/* a system or url error message, the reason given when a file could not be read. */
const WCHAR *xd_system_message(UINT code);

/* ------------------------------------------------------------------ parsing and writing, parse.c and write.c */

#define XP_STRING 0x1 /* the input is a caller's string, so the declared encoding is ignored. */

BOOL xd_parse(XDoc *doc, const WCHAR *text, UINT length, UINT flags);
/* the encoding comes from a byte order mark, then the declaration, else utf-8. */
BOOL xd_parse_bytes(XDoc *doc, const BYTE *data, UINT size);
BOOL xd_load_file(XDoc *doc, const WCHAR *location);
/* the line and line position of an offset in the input. */
void xd_line_info(const WCHAR *text, UINT length, UINT offset, LONG *line, LONG *linepos);

/* characters above max are written as references. the declaration shows its encoding when it is the one named. */
void xd_write(XNode *n, XBuf *out, BOOL indent, UINT max, const WCHAR *encoding);
/* text to bytes in an encoding, with a byte order mark when the encoding has one. */
BOOL xd_encode(const WCHAR *text, UINT length, const WCHAR *encoding, BYTE **bytes, UINT *size);
BOOL xd_valid_encoding(const WCHAR *name);
UINT xd_code_page(const WCHAR *name);
HRESULT xd_save_document(XDoc *doc, IStream *stream, const WCHAR *encoding);
HRESULT xd_save_file(XDoc *doc, const WCHAR *path);
const WCHAR *xd_decl_attr(XDoc *doc, const char *name);

/* ------------------------------------------------------------------ xpath, xpath.c */

typedef struct XSet {
    XNode **v;
    UINT n;
    UINT cap;
} XSet;

void xs_free(XSet *s);
BOOL xs_add(XSet *s, XNode *n);
/* uses the document's selection settings, the nodes come out in document order. */
HRESULT xp_select(XNode *ctx, const WCHAR *expr, XSet *out);
HRESULT xp_select_in(XNode *ctx, const WCHAR *expr, const XNs *ns, UINT nscount, BOOL xpath, XSet *out);

/* the value of an expression. a node set that is a result tree fragment has rtf set. */
enum { XPV_SET, XPV_NUM, XPV_STR, XPV_BOOL };
typedef struct XpVal {
    int t;
    double n;
    BOOL b;
    XBuf s;
    XSet set;
    BOOL rtf;
} XpVal;

void xpv_free(XpVal *v);
void xpv_string(XpVal *v, XBuf *out);
double xpv_number(XpVal *v);
BOOL xpv_bool(XpVal *v);
void xpv_set_string(XpVal *v, const WCHAR *s, UINT n);
/* a deep copy. */
BOOL xpv_copy(XpVal *to, const XpVal *from);

/* what an expression is evaluated for. current is the node current() answers. */
struct XpHost;
typedef struct XpCall {
    XNode *node;
    UINT pos;
    UINT size;
    XNode *current;
    const struct XpHost *host; /* the host of the run, else the one the expression was compiled with. */
    const XNs *ns;             /* the namespaces the expression was written in. */
    UINT nns;
} XpCall;

/* the stylesheet side of an expression: variables and the functions that are not xpath's own. */
typedef struct XpHost {
    /* a result tree fragment used as a node set: kind 0 a path or union, 1 an argument; the text is the expression. */
    HRESULT (*rtf_error)(void *user, int kind, const WCHAR *name, UINT name_len, UINT arg, const WCHAR *text,
                         UINT start, UINT end);
    BOOL (*declared)(void *user, const WCHAR *uri, const WCHAR *local, UINT length);
    BOOL (*known)(void *user, const WCHAR *uri, const WCHAR *local, UINT length, UINT args);
    HRESULT (*function)(void *user, const WCHAR *uri, const WCHAR *local, UINT length, XpVal *args, UINT count,
                        XpCall *call, XpVal *out);
    HRESULT (*variable)(void *user, const WCHAR *uri, const WCHAR *local, UINT length, XpVal *out);
    void *user;
} XpHost;

typedef struct XpExpr XpExpr;
#define XPF_XPATH 0x1   /* xpath 1.0 rules, not xslpattern. */
#define XPF_PATTERN 0x2 /* an xslt match pattern. */
#define XPF_V6 0x4
/* why an expression failed: the message id, the part of the text the message points at and its two words. */
typedef struct XpError {
    HRESULT hr;
    UINT start, end;
    WCHAR p1[40], p2[40];
} XpError;
/* the text is copied, the expression does not point into it. */
XpExpr *xp_compile(const WCHAR *text, UINT length, const XNs *ns, UINT nscount, UINT flags, const XpHost *host,
                   XDoc *doc, XpError *error);
void xp_free(XpExpr *x);
HRESULT xp_eval(XpExpr *x, XpCall *call, XpVal *out);
/* the alternatives of a pattern, in order, and the default priority of each. */
UINT xp_alternatives(XpExpr *x, XpExpr **out, UINT max);
double xp_priority(XpExpr *x);
/* whether the node matches the pattern. */
HRESULT xp_match(XpExpr *x, XNode *node, XpCall *call, BOOL *matches);
/* the plain name test a pattern or a path begins with, for indexing: the local name of a single step, else null. */
BOOL xp_not_nodeset(XpExpr *x);
/* whether a pattern is a location path of child and attribute steps, else the range of the step that is not. */
BOOL xp_pattern_ok(XpExpr *x, UINT *start, UINT *end);
enum { XPQ_NONE, XPQ_ELEMENT_NAME, XPQ_ELEMENT, XPQ_ATTRIBUTE_NAME, XPQ_ATTRIBUTE, XPQ_TEXT, XPQ_COMMENT, XPQ_PI, XPQ_ROOT };
int xp_quick(XpExpr *x, const WCHAR **local, UINT *length);
/* sorts a node set into document order and drops repeats. */
void xs_sort(XSet *s);

/* ------------------------------------------------------------------ com objects */

struct XList {
    const void *vtbl;
    LONG refs;
    XNode *base; /* referenced while the list lives. */
    UINT kind;
    WCHAR *name; /* the tag name of an element list, or the expression of a selection. */
    XNode **v;
    UINT n;
    UINT cap;
    UINT version;
    UINT cursor;
    XNode *cache_node;
    UINT cache_index;
    UINT cache_version;
    BOOL selection;
};
#define XL_CHILDREN 0
#define XL_TAGNAME 1
#define XL_NODES 2  /* a selection: the nodes were chosen once and are referenced. */
#define XL_ATTRIBUTES 3

XList *xd_list_new(XNode *base, UINT kind, const WCHAR *name, BOOL selection);
void xd_list_add_node(XList *l, XNode *n); /* for XL_NODES, takes a reference. */
BOOL xd_list_refresh(XList *l);
UINT xd_list_count(XList *l);
XNode *xd_list_item(XList *l, UINT index);

HRESULT xd_create_error_object(const XErr *e, void **out);
HRESULT xd_create_implementation(void **out);
HRESULT xd_create_enum(IUnknown *source, void **out);
HRESULT xd_get_attributes(XNode *n, void **out);
HRESULT xd_set_pi_data(XNode *n, const WCHAR *text, UINT length);
HRESULT xd_check_new_name(const WCHAR *name, UINT length);
/* a copy of a document as a document of its own, with one reference. */
XNode *xd_doc_clone(XDoc *doc, BOOL deep);
void xd_doc_vtbl_init(void);
/* adds a reference for the caller, null stays null. */
void *xd_ref(XNode *n);

/* a dispatch table row, see disp.c. */
typedef struct XDisp {
    const WCHAR *name;
    LONG id;
    WORD flags;
    WORD slot;
    const char *sig;
} XDisp;

typedef struct XDispSet {
    const XDisp *t[3];
    UINT n[3];
} XDispSet;

const XDispSet *xd_dispset_for_node(UINT type);
extern const XDispSet xd_dispset_list;
extern const XDispSet xd_dispset_selection;
extern const XDispSet xd_dispset_map;
extern const XDispSet xd_dispset_error;
extern const XDispSet xd_dispset_impl;
extern const XDispSet xd_dispset_template;
extern const XDispSet xd_dispset_processor;
HRESULT xd_create_template(BOOL v6, REFIID riid, void **out);
/* the sax reader, sax.c: the flags are those of the class. */
HRESULT xd_create_sax_reader(UINT flags, REFIID riid, void **out);
/* every byte a stream gives, from an IStream or an ISequentialStream. */
HRESULT xd_read_stream(IUnknown *object, BYTE **data, UINT *size);

HRESULT xd_disp_names(const XDispSet *set, REFIID riid, LPOLESTR *names, UINT cnames, LCID lcid, DISPID *ids);
HRESULT xd_disp_invoke(void *self, const XDispSet *set, DISPID id, REFIID riid, LCID lcid, WORD flags,
                       DISPPARAMS *params, VARIANT *result, EXCEPINFO *excep, UINT *argerr);

typedef struct XClass {
    const CLSID *clsid;
    UINT flags;          /* XDF_ bits the document starts with. */
    const WCHAR *progid; /* may be null. */
    const WCHAR *version_independent;
    const WCHAR *name;
    UINT kind; /* 0 a document, 1 an xslt template, 2 a sax reader */
} XClass;
#define XK_DOCUMENT 0
#define XK_TEMPLATE 1
#define XK_SAXREADER 2

HRESULT xd_get_class_object(const XClass *classes, UINT count, REFCLSID clsid, REFIID riid, void **out);
HRESULT xd_can_unload(void);
HRESULT xd_register_server(const XClass *classes, UINT count, HMODULE module, BOOL unregister);

BOOL xv_to_string(const VARIANT *v, XBuf *out);
BOOL xv_to_long(const VARIANT *v, LONG *out);
BOOL xv_to_bool(const VARIANT *v, BOOL *out);
BOOL xd_node_is_ours(const void *p);
WCHAR *xd_location_to_path(const WCHAR *location, BOOL *other);
const VARIANT *xv_deref(const VARIANT *v);
BOOL xv_is_missing(const VARIANT *v);
/* the node a variant holds, or null. the caller releases it. */
XNode *xv_to_node(const VARIANT *v);
/* S_OK with the string, or S_FALSE and nothing when the string is null. */
HRESULT xd_ret_string(BSTR *out, const WCHAR *s, UINT length);
HRESULT xd_ret_buf(BSTR *out, XBuf *b);
HRESULT xd_ret_node(XNode *n, void **out);
/* the text of the last failure of a transformation on this thread, handed out once for the exception info. */
void xd_set_error_text(HRESULT hr, const WCHAR *text, BOOL v6);
WCHAR *xd_take_error_text(HRESULT hr, BOOL *v6);
BOOL xd_list_is_ours(const void *p);

/* ------------------------------------------------------------------ interface tables */

#include <mime/win32/msxml.h>
#include <xmldom/vtables.h>

#endif
