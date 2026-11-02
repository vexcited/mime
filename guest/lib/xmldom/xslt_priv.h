/* the state of one transformation, shared by the files that run it. */
#ifndef MIME_XMLDOM_XSLT_PRIV_H
#define MIME_XMLDOM_XSLT_PRIV_H

#include <xmldom/xslt.h>

typedef struct XsBinding {
    const WCHAR *uri;
    const WCHAR *local;
    UINT llen;
    XpVal val;
} XsBinding;

/* the context an instruction runs in. */
typedef struct XsFrame {
    XNode *node;
    UINT pos;
    UINT size;
} XsFrame;

typedef struct XsLoaded {
    WCHAR *url;
    XNode *root; /* the document node, null when it did not load. */
} XsLoaded;

typedef struct XsKeyEntry {
    const WCHAR *value;
    UINT length;
    XNode *node;
    UINT hash;
} XsKeyEntry;

typedef struct XsKeyIndex {
    const WCHAR *uri;
    const WCHAR *local;
    UINT llen;
    XDoc *doc;
    XsKeyEntry *entries; /* sorted by value then document order */
    UINT n;
    WCHAR *pool;
} XsKeyIndex;

typedef struct XsScriptHost XsScriptHost;

struct XsRun {
    XsRequest *req;
    XsSheet *sheet;
    BOOL v6;
    XDoc *out;
    XNode *cur;
    XsBinding *stack;
    UINT sp, cap, base;
    XpVal *gval;
    UINT8 *gstate;
    XDoc **temps; /* documents made for fragments and loaded for document(), released at the end */
    UINT ntemps, captemps;
    XsLoaded *loaded;
    UINT nloaded, caploaded;
    XsKeyIndex *keys;
    UINT nkeys, capkeys;
    XDoc **seen; /* documents in the order generate-id met them */
    UINT nseen, capseen;
    XpHost host;
    UINT frames;
    XsTemplate *tmpl; /* the template being run, for xsl:apply-imports */
    XNode *tmpl_node;
    const WCHAR *tmpl_mode_uri;
    const WCHAR *tmpl_mode;
    XsError *err;
    XsScriptHost *scripts;
    XNode *input_root;
    XNode *stripped_from;
    XNode *stripped_to;
    XsParam *cur_params; /* parameters of the template being called */
    UINT8 *stack_base;
    BOOL aborted;
    BOOL on_big_stack;
};

HRESULT xsr_error(XsRun *r, UINT id, const WCHAR *p1, const WCHAR *p2, const WCHAR *p3);

/* xslt_fn.c */
HRESULT xsr_function(void *user, const WCHAR *uri, const WCHAR *local, UINT length, XpVal *args, UINT count,
                     XpCall *call, XpVal *out);
BOOL xsr_known_function(void *user, const WCHAR *uri, const WCHAR *local, UINT length, UINT args);
const WCHAR *xsr_doc_serial_id(XsRun *r, XNode *n, XBuf *out);
void xsr_free_state(XsRun *r);
/* the string of a node for the xpath and xslt functions, with the white space msxml hides. */
void xsr_node_string(XNode *n, XBuf *out);

/* xslt_num.c */
HRESULT xsr_number(XsRun *r, XsInst *inst, XsFrame *f, XBuf *out);
/* the xsl:number format string applied to a list of numbers. */
void xsr_format_numbers(const WCHAR *format, UINT flen, const double *values, UINT n, const WCHAR *group_sep,
                        UINT group_size, const WCHAR *letter_value, XBuf *out);

/* xslt_script.c */
HRESULT xsr_call_extension(XsRun *r, const WCHAR *uri, const WCHAR *local, UINT length, XpVal *args, UINT count,
                           XpCall *call, XpVal *out);
BOOL xsr_has_script_function(XsRun *r, const WCHAR *uri, const WCHAR *local, UINT length);
void xsr_script_free(XsRun *r);
HRESULT xsr_start_scripts(XsRun *r);
/* a value of the processor as a variant, and back. */
HRESULT xsr_val_to_variant(XsRun *r, XpVal *v, VARIANT *out);
HRESULT xsr_variant_to_val(XsRun *r, const VARIANT *v, XpVal *out);

/* helpers shared in the run files */
XDoc *xsr_new_temp_doc(XsRun *r);
XNode *xsr_load_document(XsRun *r, const WCHAR *url, UINT len, const WCHAR *base);
HRESULT xsr_make_nodeset_of_rtf(XsRun *r, XpVal *v);

#endif
