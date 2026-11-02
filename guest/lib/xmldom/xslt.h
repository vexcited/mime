/*
 * xslt 1.0 for msxml3.dll and msxml6.dll: a stylesheet is compiled into a tree of instructions, a transformation walks
 * it and builds a result tree, which a serializer writes as xml, html or text.
 */
#ifndef MIME_XMLDOM_XSLT_H
#define MIME_XMLDOM_XSLT_H

#include <xmldom/xmldom.h>

#define XSLT_NS L"http://www.w3.org/1999/XSL/Transform"
#define MSXSL_NS L"urn:schemas-microsoft-com:xslt"
#define XML_NS L"http://www.w3.org/XML/1998/namespace"

/* error ids that are not in the message tables. */
#define XE_XS_STACK 0xC00CE300u
#define XE_XS_KEYWORD_HERE 0xC00CE301u
#define XE_XS_ROOT_NOT_ELEMENT 0xC00CE303u
#define XE_XS_KEYWORD_FOLLOW 0xC00CE304u
#define XE_XS_NOT_LANGUAGE 0xC00CE305u
#define XE_XS_KEYWORD_CONTAIN 0xC00CE30Bu
#define XE_XS_KEYWORD_NS 0xC00CE30Cu
#define XE_XS_ATTR_ONLY 0xC00CE30Du
#define XE_XS_INCLUDE_SELF 0xC00CE30Fu
#define XE_XS_BAD_URL 0xC00CE310u
#define XE_XS_NO_DOCELEM 0xC00CE311u
#define XE_XS_PARSE_ERROR 0xC00CE312u
#define XE_XS_BAD_PRIORITY 0xC00CE313u
#define XE_XS_VAR_NOT_SET 0xC00CE314u
#define XE_XS_VAR_TWICE 0xC00CE315u
#define XE_XS_VAR_UNRESOLVED 0xC00CE316u
#define XE_XS_SCRIPT_LANGS 0xC00CE317u
#define XE_XS_NS_NO_FUNCTIONS 0xC00CE318u
#define XE_XS_NS_NO_FUNCTION 0xC00CE319u
#define XE_XS_NO_RETURN 0xC00CE320u
#define XE_XS_CALL_ERROR 0xC00CE321u
#define XE_XS_PARAM_ERROR 0xC00CE322u
#define XE_XS_ATTR_BAD 0xC00CE323u
#define XE_XS_VAR_CIRCULAR 0xC00CE324u
#define XE_XS_KEYWORD_NODES 0xC00CE325u
#define XE_XS_VAR_BOTH 0xC00CE326u
#define XE_XS_TEMPLATE_DUP 0xC00CE327u
#define XE_XS_TEMPLATE_MISSING 0xC00CE328u
#define XE_XS_GLOBAL_DUP 0xC00CE329u
#define XE_XS_IMPORTS_FOREACH 0xC00CE32Au
#define XE_XS_ATTRSET_MISSING 0xC00CE32Bu
#define XE_XS_ATTRSET_CIRCULAR 0xC00CE32Cu
#define XE_XS_INCLUDE_ERROR 0xC00CE32Du
#define XE_XS_ATTR_INVALID 0xC00CE32Eu
#define XE_XS_CHOOSE_EMPTY 0xC00CE32Fu
#define XE_XS_BAD_PREFIX 0xC00CE330u
#define XE_XS_NOT_EXTENSION 0xC00CE331u
#define XE_XS_KEY_MISSING 0xC00CE333u
#define XE_XS_BAD_FUNCTION 0xC00CE334u
#define XE_XS_RETURN_NODESET 0xC00CE335u
#define XE_XS_ARG_NODESET 0xC00CE336u
#define XE_XS_MODE_NOMATCH 0xC00CE337u
#define XE_XS_PATTERN_FUNCTION 0xC00CE338u
#define XE_XS_SINGLE_CHAR 0xC00CE339u
#define XE_XS_DECIMAL_REDEF 0xC00CE33Au
#define XE_XS_DECIMAL_MISSING 0xC00CE33Bu
#define XE_XS_SCRIPT_BLOCKED 0xC00CE33Cu
#define XE_XS_BAD_LANGUAGE 0xC00CE33Du
#define XE_XS_PARAM_TWICE 0xC00CE342u
#define XE_XS_WD_XSL 0xC00CE344u
#define XE_XS_NOT_NODESET 0xC00CE617u
#define XE_XS_BAD_PATTERN 0xC00CE618u

typedef struct XsSheet XsSheet;
typedef struct XsInst XsInst;
typedef struct XsRun XsRun;

/* an error of the stylesheet or the transformation: a message id and the text for its places. */
typedef struct XsError {
    UINT id;
    WCHAR *p[3];
    WCHAR *suffix; /* appended after a blank line, such as the expression with the place of the error marked. */
    WCHAR *text;   /* the whole text, for an error that is only one. */
    HRESULT hr;
} XsError;

void xse_clear(XsError *e);
HRESULT xse_set(XsError *e, UINT id, const WCHAR *p1, const WCHAR *p2, const WCHAR *p3);
HRESULT xse_set_text(XsError *e, HRESULT hr, const WCHAR *text);
HRESULT xse_name_error(XsError *e, const WCHAR *s, UINT len, UINT bad);
WCHAR *xse_marked_text(const WCHAR *text, UINT length, UINT start, UINT end);
/* the text of the error as the processor words it. */
WCHAR *xse_text(const XsError *e, BOOL v6);

/* attribute value templates. */
typedef struct XsAvtPart {
    const WCHAR *lit;
    UINT len;
    XpExpr *expr;
} XsAvtPart;

typedef struct XsAvt {
    XsAvtPart *parts;
    UINT n;
} XsAvt;

/* the namespaces in scope at an element of the stylesheet. */
typedef struct XsScope XsScope;
struct XsScope {
    XsScope *parent;
    XNs *decl;
    UINT n;
    XNs *flat; /* all of them, built when needed. */
    UINT nflat;
};

/* a qualified name resolved when the stylesheet is compiled. */
typedef struct XsName {
    const WCHAR *prefix; /* may be null */
    UINT plen;
    const WCHAR *uri; /* null for none */
    const WCHAR *local;
    UINT llen;
    const WCHAR *qname;
    UINT qlen;
} XsName;

enum {
    XO_TEXT,
    XO_LRE,
    XO_VALUE_OF,
    XO_COPY_OF,
    XO_APPLY,
    XO_APPLY_IMPORTS,
    XO_CALL,
    XO_FOR_EACH,
    XO_IF,
    XO_CHOOSE,
    XO_WHEN,
    XO_OTHERWISE,
    XO_VARIABLE,
    XO_PARAM,
    XO_WITH_PARAM,
    XO_ELEMENT,
    XO_ATTRIBUTE,
    XO_COMMENT,
    XO_PI,
    XO_COPY,
    XO_NUMBER,
    XO_SORT,
    XO_MESSAGE,
    XO_FALLBACK,
    XO_SCRIPT_CALL,
};

typedef struct XsAttr {
    XsName name;
    XsAvt *value;
} XsAttr;

struct XsInst {
    int op;
    XsInst *next;
    XsInst *body;
    XsInst *sorts;  /* the xsl:sort children of apply-templates and for-each. */
    XsInst *params; /* the xsl:with-param children of call-template and apply-templates. */
    XpExpr *select;
    XpExpr *test;
    XpExpr *count;
    XpExpr *from;
    XsAvt *avt[8]; /* name, namespace, and the other attributes that are templates. */
    XsName name;   /* of a variable, a parameter, a called template, a literal element. */
    XsName mode;
    BOOL has_mode;
    BOOL has_select;
    const WCHAR *text;
    UINT tlen;
    BOOL doe;
    XsScope *scope;
    XsAttr *attrs; /* of a literal result element. */
    UINT nattrs;
    XNs *nsdecl; /* namespace declarations a literal result element writes. */
    UINT nnsdecl;
    XsName *sets; /* attribute sets the element uses. */
    UINT nsets;
    UINT line;
    int level; /* xsl:number */
    BOOL fallback_only;
    BOOL in_for_each;
    struct XsTemplate *tmpl; /* the template an instruction is in. */
    const WCHAR *terminate;
    void *aux;
};

typedef struct XsTemplate {
    XpExpr *match;
    XpExpr *pattern; /* one alternative of the match. */
    double priority;
    XsName name;
    BOOL has_name;
    XsName mode;
    int precedence;
    UINT order;
    XsInst *params;
    XsInst *body;
    struct XsModule *module;
    BOOL has_match;
    int quick; /* what the pattern can match, from xp_quick */
    const WCHAR *quick_local;
    UINT quick_len;
} XsTemplate;

typedef struct XsModule {
    int precedence;
    int import_min; /* the lowest precedence among the modules it imports. */
    WCHAR *url;
} XsModule;

typedef struct XsKey {
    XsName name;
    XpExpr *match;
    XpExpr *use;
    int precedence;
} XsKey;

typedef struct XsDecimal {
    XsName name;
    BOOL has_name;
    WCHAR decimal, grouping, percent, permille, zero, digit, pattern, minus;
    WCHAR *infinity;
    WCHAR *nan;
    WCHAR chars[8]; /* the eight characters in the order of the attributes written in the stylesheet. */
    UINT explicit_mask;
} XsDecimal;

typedef struct XsGlobal {
    XsName name;
    BOOL is_param;
    XsInst *inst;
    int precedence;
    int state; /* 0 not evaluated, 1 evaluating, 2 done */
    XpVal value;
} XsGlobal;

typedef struct XsAttrSet {
    XsName name;
    XsInst *body; /* xsl:attribute instructions */
    XsName *use;
    UINT nuse;
    int precedence;
} XsAttrSet;

typedef struct XsOutput {
    int method; /* 0 unset, 1 xml, 2 html, 3 text */
    WCHAR *version;
    WCHAR *encoding;
    int omit_decl;   /* -1 unset */
    WCHAR *standalone;
    WCHAR *doctype_public;
    WCHAR *doctype_system;
    int indent; /* -1 unset */
    WCHAR *media_type;
    UINT ncdata, capcdata;
    XsName *cdata_names;
} XsOutput;

typedef struct XsSpace {
    BOOL strip;
    int kind; /* 0 a name, 1 any name in a namespace, 2 any */
    const WCHAR *uri;
    const WCHAR *local;
    UINT llen;
    int precedence;
    UINT order;
} XsSpace;

typedef struct XsScript {
    WCHAR *uri;      /* the namespace the functions are in. */
    WCHAR *language; /* from the language attribute. */
    WCHAR *prefix;
    WCHAR *text;
    BOOL has_implements;
    WCHAR *base; /* the location of the stylesheet. */
    UINT line;
    XsScope *scope;
} XsScript;

struct XsSheet {
    LONG refs;
    BOOL v6;
    BOOL wd; /* the old dialect */
    double version;
    XsTemplate **templates;
    UINT ntemplates, captemplates;
    XsGlobal *globals;
    UINT nglobals, capglobals;
    XsKey *keys;
    UINT nkeys, capkeys;
    XsDecimal *decimals;
    UINT ndecimals, capdecimals;
    XsAttrSet *sets;
    UINT nsets, capsets;
    XsSpace *spaces;
    UINT nspaces, capspaces;
    XsOutput output;
    XsScript *scripts;
    UINT nscripts, capscripts;
    XsModule **modules;
    UINT nmodules, capmodules;
    XsInst *simplified; /* the body of a simplified stylesheet, which has no template. */
    XNode *literal; /* a document that is no stylesheet is its own result. */
    XNode *source;  /* a copy of the stylesheet document for document(''), made when something asks for it. */
    BOOL wants_source;
    XpExpr **exprs;
    UINT nexprs, capexprs;
    void *arena;
    UINT8 *cur, *end;
    WCHAR *url;
    BOOL has_strip;
    BOOL allow_script;
    BOOL resolve_externals;
    XsError error;
    UINT order;
    int next_precedence;
    XNs *aliases;
    XNs *alias_target;
    UINT naliases;
};

/* ------------------------------------------------------------------ compiling, xslt_compile.c */

/* the node is the document or the element of the stylesheet. null with the error filled in on failure. */
XsSheet *xs_compile(XNode *style, UINT flags, XsError *error);
void xs_sheet_release(XsSheet *sheet);
const WCHAR *xs_scope_lookup(const XsScope *sc, const WCHAR *prefix, UINT plen, BOOL *found);
WCHAR *xs_join_location(const WCHAR *base, const WCHAR *href, UINT hlen);
void xs_sheet_addref(XsSheet *sheet);
void *xs_alloc(XsSheet *sheet, SIZE_T size);
void *xs_grow(XsSheet *s, void *old, UINT n, UINT cap, SIZE_T size);
WCHAR *xs_dup(XsSheet *sheet, const WCHAR *s, UINT len);

#define XSF_V6 0x1
#define XSF_ALLOW_SCRIPT 0x2
#define XSF_RESOLVE 0x4

/* ------------------------------------------------------------------ running, xslt_run.c */

typedef struct XsParam {
    WCHAR *name;
    WCHAR *uri;
    VARIANT value;
} XsParam;

typedef struct XsObject {
    WCHAR *uri;
    IDispatch *object;
} XsObject;

typedef struct XsRequest {
    XsSheet *sheet;
    XNode *input;
    XsParam *params;
    UINT nparams;
    XsObject *objects;
    UINT nobjects;
    const WCHAR *mode;
    const WCHAR *mode_uri;
    BOOL v6;
    XDoc *result; /* the result tree, which the caller owns after the run. */
    XsError error;
    BOOL (*onmessage)(void *user, const WCHAR *text);
    void *user;
    BOOL allow_doc_fn;
    BOOL allow_script;
    HRESULT (*progress)(void *user);
} XsRequest;

HRESULT xs_run(XsRequest *req);

/* ------------------------------------------------------------------ serializing, xslt_out.c */

/* the result tree as text. the encoding is the one the text is for, null for utf-16 text. */
typedef struct XsSerial {
    int method;
    BOOL v6;
    BOOL to_string; /* the text goes to a string, so the encoding is utf-16 whatever the output says. */
} XsSerial;

/* the method the output ends up with: the one asked for, else html when the root element is html. */
int xs_output_method(const XsOutput *out, XDoc *result);
void xs_serialize(const XsOutput *out, XDoc *result, BOOL v6, XBuf *text, const WCHAR *forced_encoding);
/* the encoding of the output as the stylesheet names it, utf-8 when it names none. */
const WCHAR *xs_output_encoding(const XsOutput *out);

/* ------------------------------------------------------------------ com, xslt_com.c */

HRESULT xs_transform_node(XNode *input, XNode *style, VARIANT *output, BSTR *text);
XsSheet *xs_compile_for(XNode *style, XsError *err);
void xs_result_text(XsSheet *sheet, XDoc *result, BOOL v6, XBuf *text, const WCHAR *encoding);

#endif
