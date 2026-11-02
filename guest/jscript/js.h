/*
 * jscript.dll internals: values, objects and the collector, the syntax tree, and what the source files share.
 * the engine is JScript 5.8, ES3 with Microsoft's extensions, run by walking the tree.
 */
#ifndef MIME_JS_H
#define MIME_JS_H

#include <mime/win32/winbase.h>
#include <mime/win32/winuser.h>
#include <mime/win32/objbase.h>
#include <mime/win32/oleauto.h>
#include <mime/win32/activscp.h>
#include <rtl.h>

WINOLEAUTAPI HRESULT WINAPI CoGetObject(LPCWSTR name, void *options, REFIID riid, void **object);

double sin(double), cos(double), tan(double), atan(double), exp(double), log(double), sqrt(double), pow(double, double);
double floor(double), ceil(double), fmod(double, double), modf(double, double *), asin(double), acos(double);
double atan2(double, double), fabs(double), strtod(const char *, char **), wcstod(const wchar_t *, wchar_t **);
int iswalpha(unsigned short c);
int iswalnum(unsigned short c);
void qsort(void *base, size_t count, size_t size, int (*compare)(const void *, const void *));

typedef struct Engine Engine;
typedef struct Obj Obj;
typedef struct Str Str;
typedef struct Node Node;
typedef struct FuncNode FuncNode;
typedef struct Prop Prop;
typedef struct VarBox VarBox;
struct JsDisp;

/* ------------------------------------------------------------------ memory and the collector */

enum { GC_STR = 1, GC_OBJ, GC_PROPS, GC_VALS, GC_VARBOX, GC_BYTES, GC_INDEX, GC_MATCHES, GC_ITEMS };

void *gc_alloc(Engine *e, SIZE_T size, int type);
void gc_collect(Engine *e);
void gc_add_root(Engine *e, void *payload);
void *js_alloc(SIZE_T size);
void *js_realloc(void *p, SIZE_T size);
void js_free(void *p);

/* ------------------------------------------------------------------ values */

enum { T_UNDEF, T_NULL, T_BOOL, T_NUM, T_STR, T_OBJ, T_VAR, T_HOLE };

typedef struct Val {
    BYTE t, r8;
    union {
        BOOL b;
        double n;
        Str *s;
        Obj *o;
        VarBox *v;
    } u;
} Val;

struct Str {
    ULONG len;
    ULONG hash;
    WCHAR s[1];
};

/* a variant of a type the engine has no value for, carried as it came. */
struct VarBox {
    VARIANT v;
};

#define V_IS_UNDEF(v) ((v).t == T_UNDEF)
#define UNDEF ((Val){T_UNDEF, 0, {0}})

static inline Val v_num(double n) { Val v = {T_NUM, 0, {0}}; v.u.n = n; return v; }
/* a number the host must see as a double even when it is whole. */
static inline Val v_dbl(double n) { Val v = {T_NUM, 1, {0}}; v.u.n = n; return v; }
static inline Val v_bool(BOOL b) { Val v = {T_BOOL, 0, {0}}; v.u.b = b != 0; return v; }
static inline Val v_str(Str *s) { Val v = {T_STR, 0, {0}}; v.u.s = s; return v; }
static inline Val v_obj(Obj *o) { Val v = {T_OBJ, 0, {0}}; v.u.o = o; return v; }
static inline Val v_null(void) { Val v = {T_NULL, 0, {0}}; return v; }

/* ------------------------------------------------------------------ objects */

enum {
    C_OBJECT, C_FUNCTION, C_ARRAY, C_STRING, C_NUMBER, C_BOOLEAN, C_DATE, C_REGEXP, C_ERROR, C_ARGS, C_ACTIVATION, C_WITH,
    C_HOST, C_ENUMERATOR, C_VBARRAY, C_MATH, C_GLOBAL, C_DEBUG, C_REGEXP_STATIC
};

#define PF_DONTENUM 1
#define PF_DONTDELETE 2
#define PF_READONLY 4

struct Prop {
    Str *key;
    Val v;
    ULONG flags;
    DISPID id;
};

typedef int (*NativeFn)(Engine *e, Val *self, Val *args, int argc, Val *ret, BOOL construct);

typedef struct NativeFunc {
    NativeFn Fn;
    int Magic;
} NativeFunc;

struct Obj {
    BYTE cls, flags, pad[2];
    Obj *proto;
    Prop *props;
    ULONG pcount, pcap, ndead;
    ULONG *index;
    ULONG icap;
    LONG ext;
    union {
        struct {
            Val prim;
        } prim;
        struct {
            FuncNode *node;
            Obj *scope;
            NativeFn native;
            int magic;
            Obj *home;
        } fn;
        struct {
            Val *items;
            ULONG len, cap;
        } arr;
        struct {
            Obj *outer;
            Obj *target;
            Obj *func;
            Val *args;
            int argc, mapped;
            Obj *arguments;
        } scope;
        struct {
            IDispatch *disp;
            VARIANT *self;
            BOOL Callable;
        } host;
        struct {
            struct Regex *re;
            Str *source;
            ULONG flags;
        } re;
        struct {
            IEnumVARIANT *en;
            VARIANT cur;
            BOOL at_end, loaded;
        } en;
        struct {
            SAFEARRAY *sa;
        } vba;
        struct {
            Val *vals;
            ULONG n;
        } err;
    } u;
};

#define OF_EXTENSIBLE 1
#define OF_COMPILE 4

/* ------------------------------------------------------------------ the syntax tree */

enum {
    N_NUM, N_STR, N_REGEX, N_TRUE, N_FALSE, N_NULL, N_THIS, N_IDENT, N_ARRAY, N_OBJECT, N_FUNC, N_MEMBER, N_INDEX,
    N_CALL, N_NEW, N_UNARY, N_POSTFIX, N_PREFIX, N_BINARY, N_LOGICAL, N_COND, N_ASSIGN, N_COMMA, N_VAR, N_EXPR,
    N_BLOCK, N_IF, N_FOR, N_FORIN, N_WHILE, N_DO, N_CONTINUE, N_BREAK, N_RETURN, N_WITH, N_SWITCH, N_CASE, N_LABEL,
    N_THROW, N_TRY, N_EMPTY, N_FUNCDECL, N_PROP, N_ELISION, N_CCSET
};

enum {
    TK_EOF, TK_IDENT, TK_NUM, TK_STR, TK_REGEX, TK_PUNCT, TK_KEYWORD
};

/* operators are the characters or these. */
enum {
    OP_NONE, OP_LE = 128, OP_GE, OP_EQ, OP_NE, OP_SEQ, OP_SNE, OP_SHL, OP_SHR, OP_USHR, OP_AND, OP_OR, OP_INC, OP_DEC,
    OP_ADDA, OP_SUBA, OP_MULA, OP_DIVA, OP_MODA, OP_SHLA, OP_SHRA, OP_USHRA, OP_ANDA, OP_ORA, OP_XORA,
    OP_TYPEOF, OP_VOID, OP_DELETE, OP_IN, OP_INSTANCEOF
};

struct Node {
    BYTE kind, op, flag, pad;
    ULONG line, col, start, end;
    Node *a, *b, *c, *d;
    Node *next;
    union {
        double num;
        Str *str;
        FuncNode *fn;
        ULONG count;
    } u;
};

struct FuncNode {
    Node n;
    Str *name;
    Str **params;
    ULONG nparams;
    Node *body;
    Str **vars;
    ULONG nvars;
    Node **funcs;
    ULONG nfuncs;
    BYTE uses_args, uses_eval, is_expr, is_global;
    struct Unit *unit;
};

struct Arena;
typedef struct Unit {
    WCHAR *text;
    ULONG length;
    ULONG first_line;
    DWORD_PTR cookie;
    struct Arena *arena;
    FuncNode *program;
    struct Unit *next;
} Unit;

/* ------------------------------------------------------------------ the engine */

typedef struct NamedItem {
    Str *name;
    DWORD flags;
    Val value;
    struct NamedItem *next;
} NamedItem;

enum { ERR_ERROR, ERR_EVAL, ERR_RANGE, ERR_REFERENCE, ERR_SYNTAX, ERR_TYPE, ERR_URI, ERR_REGEXP, ERR_COUNT };

typedef struct Fault {
    BOOL Set;
    HRESULT Scode;
    BSTR Source, Description, HelpFile;
    ULONG Line, Col;
    BOOL Compile;
    PWSTR Text;
    DWORD_PTR Cookie;
    BOOL Host;
} Fault;

typedef struct CallFrame {
    struct CallFrame *prev;
    Obj *func;
    Node *stmt;
    Obj *act;
} CallFrame;

typedef struct NameEntry {
    Str *name;
} NameEntry;

struct Engine {
    IActiveScript Script;
    IActiveScriptParse Parse;
    IActiveScriptParseProcedure2 Procedure;
    IActiveScriptProperty Property;
    IObjectSafety Safety;
    IActiveScriptGarbageCollector Collector;
    LONG Refs;
    IActiveScriptSite *Site;
    SCRIPTSTATE State;
    LCID Lcid;
    DWORD SafetyOptions;
    BOOL Closing, InScript, Interrupted, CompileOnly;
    UINT NestedRun;
    DWORD Thread;
    NamedItem *Items;
    Unit *Units, *Pending;
    Fault Fault;
    BSTR *Pad;

    /* the collector. */
    struct GcHdr *Heap;
    SIZE_T HeapBytes, Threshold;
    BOOL InGc, NoGc;
    void **Roots;
    ULONG RootCount, RootCap;
    void **Deferred;
    ULONG DeferredCount, DeferredCap;
    void *StackBase;

    /* names. */
    Str **Interned;
    ULONG InternCount, InternCap;
    Str **DispNames;
    ULONG DispCount, DispCap;

    /* the global state. */
    Obj *Global, *ObjectProto, *FunctionProto, *ArrayProto, *StringProto, *NumberProto, *BooleanProto, *DateProto,
        *RegExpProto, *ErrorProto, *ErrorProtos[ERR_COUNT], *EnumeratorProto, *VBArrayProto;
    Obj *ErrorCtors[ERR_COUNT];
    Obj *RegExpCtor;
    Obj *ArrayCtor;
    Val Exception;
    Node *ErrNode;
    ULONG ErrLine, ErrCol;
    BOOL ErrFromHost;
    HRESULT ErrScode;
    BSTR ErrSource, ErrDescription;
    BOOL ErrSyntax;
    CallFrame *Frames;
    UINT Depth;
    Node *CurStmt;
    Node *EvalStmt;
    int CcOn, CcVersion;
    Val LastMatch[10];
    Str *LastInput;
    ULONG LastIndex, LastLastIndex, LastMatchCount;
    ULONG LastCaptures[20];
    double RndSeed;
    BOOL RndInit;
    Obj *ScriptDispatch;
    ULONG Epoch;
    int Magic, CallCtx;
    Val RetVal, GlobalThis;
    BOOL SoftPrim, NoPrim, EnterNotified, Encoded;
    ULONG Polls;
    IActiveScriptSiteInterruptPoll *PollSite;
    struct JsDisp *Disps;
    DWORD_PTR ErrCookie;
    BOOL Abort, LastCapturesValid;
};

/* ------------------------------------------------------------------ completion codes */

#define RC_OK 0
#define RC_THROW 1
#define RC_BREAK 2
#define RC_CONTINUE 3
#define RC_RETURN 4

/* ------------------------------------------------------------------ value.c */

Str *str_new(Engine *e, const WCHAR *s, SIZE_T len);
Str *str_alloc(Engine *e, SIZE_T len);
Str *str_cat(Engine *e, Str *a, Str *b);
Str *str_from_z(Engine *e, const WCHAR *z);
Str *str_from_ascii(Engine *e, const char *z);
Str *str_intern(Engine *e, const WCHAR *s, SIZE_T len);
Str *str_intern_str(Engine *e, Str *s);
Str *str_intern_z(Engine *e, const WCHAR *z);
Str *str_intern_ascii(Engine *e, const char *z);
ULONG str_hash(Str *s);
BOOL str_equal(Str *a, Str *b);
int str_compare(Str *a, Str *b);
Str *str_empty(Engine *e);
Str *num_to_str(Engine *e, double n);
int double_digits(double x, int count, char *out);
int shortest_digits(double x, char *out);
Str *num_to_radix(Engine *e, double n, int radix);
double str_to_num(Str *s);
BOOL str_to_index(Str *s, ULONG *index);
Str *index_to_str(Engine *e, ULONG index);

int to_primitive(Engine *e, Val *v, int hint, Val *out);
int to_number(Engine *e, Val *v, double *out);
int to_string(Engine *e, Val *v, Str **out);
int to_boolean_val(Val *v);
int to_object(Engine *e, Val *v, Obj **out);
int to_int32(Engine *e, Val *v, LONG *out);
int to_uint32(Engine *e, Val *v, ULONG *out);
double to_integer_d(double n);
LONG d_to_int32(double n);
ULONG d_to_uint32(double n);
BOOL strict_equals(Val *a, Val *b);
int loose_equals(Engine *e, Val *a, Val *b, BOOL *result);
int compare_values(Engine *e, Val *a, Val *b, BOOL left_first, int *result);
const WCHAR *typeof_name(Val *v);
BOOL is_callable(Val *v);
BOOL is_nan(double d);
BOOL is_js_space(WCHAR c);
WCHAR js_tolower(WCHAR c);
WCHAR js_toupper(WCHAR c);
BOOL host_same_object(Obj *a, Obj *b);
BOOL is_inf(double d);
extern const double js_nan, js_pnan;

/* ------------------------------------------------------------------ object.c */

Obj *obj_new(Engine *e, int cls, Obj *proto);
Prop *obj_find_own(Obj *o, Str *key);
int obj_get(Engine *e, Obj *o, Str *key, Val *out);
int obj_get_this(Engine *e, Obj *o, Str *key, Val *self, Val *out);
int obj_put(Engine *e, Obj *o, Str *key, Val *v);
BOOL obj_has(Engine *e, Obj *o, Str *key);
BOOL obj_has_own(Engine *e, Obj *o, Str *key);
int obj_delete(Engine *e, Obj *o, Str *key, BOOL *result);
void obj_define(Engine *e, Obj *o, Str *key, Val v, ULONG flags);
void obj_define_z(Engine *e, Obj *o, const char *name, Val v, ULONG flags);
Obj *obj_native(Engine *e, const char *name, NativeFn fn, int argc, int magic);
void obj_method(Engine *e, Obj *o, const char *name, NativeFn fn, int argc, int magic);
int obj_get_index(Engine *e, Obj *o, ULONG i, Val *out);
int obj_put_index(Engine *e, Obj *o, ULONG i, Val *v);
Obj *array_new(Engine *e, ULONG len);
int array_set_length(Engine *e, Obj *a, double len);
void array_push(Engine *e, Obj *a, Val v);
int call_function(Engine *e, Val *func, Val *self, Val *args, int argc, Val *ret);
int construct_function(Engine *e, Val *func, Val *args, int argc, Val *ret);
int obj_default_value(Engine *e, Obj *o, int hint, Val *out);
int to_primitive_soft(Engine *e, Val *v, int hint, Val *out, BOOL *none);
int obj_enum_keys(Engine *e, Obj *o, Obj **out_array, BOOL include_dontenum);
const WCHAR *class_name(Obj *o);
int throw_error(Engine *e, int kind, int code, const WCHAR *detail);
int throw_value(Engine *e, Val v);
int throw_host_error(Engine *e, HRESULT hr, const WCHAR *source, const WCHAR *description);
Obj *make_error(Engine *e, int kind, int code, const WCHAR *detail);
PCWSTR error_text(int code);
BOOL error_known(int code);
int error_kind_of(int code);
Obj *make_error_text(Engine *e, HRESULT number, const WCHAR *message, SIZE_T n, BOOL with_number);
int throw_number(Engine *e, HRESULT hr);
BOOL obj_instanceof(Engine *e, Obj *o, Obj *ctor, BOOL *result);
Prop *obj_add_prop(Engine *e, Obj *o, Str *key, Val v, ULONG flags);

/* ------------------------------------------------------------------ parse.c */

typedef struct ParseError {
    BOOL Failed;
    int Code;
    ULONG Line, Col;
    WCHAR *LineText;
} ParseError;
Unit *parse_unit(Engine *e, const WCHAR *text, SIZE_T length, ULONG first_line, DWORD_PTR cookie, int mode, ParseError *err);
void unit_free(Unit *u);
void *arena_alloc(struct Arena *a, SIZE_T size);
Str *regex_parse_literal(Engine *e, const WCHAR *src, SIZE_T len);
PWSTR js_wcsdup(const WCHAR *s);

/* ------------------------------------------------------------------ interp.c */

int run_program(Engine *e, Unit *u, Obj *scope, Val *result, BOOL want_value);
int call_node_function(Engine *e, Obj *func, Val *self, Val *args, int argc, Val *ret, BOOL construct);
int eval_code(Engine *e, Str *code, Obj *scope, Val *self, Val *out);
Obj *func_new_node(Engine *e, FuncNode *f, Obj *scope);
Obj *func_new_native(Engine *e, const WCHAR *name, NativeFn fn, int argc, int magic);

/* ------------------------------------------------------------------ builtins */

void init_globals(Engine *e);
void init_object(Engine *e);
void init_function(Engine *e);
void init_array(Engine *e);
void init_string(Engine *e);
void init_number(Engine *e);
void init_math(Engine *e);
void init_date(Engine *e);
void init_regexp(Engine *e);
void init_error(Engine *e);
void init_host(Engine *e);
Obj *make_regexp(Engine *e, Str *source, const WCHAR *flags, SIZE_T flag_len, BOOL *bad);
int regexp_exec(Engine *e, Obj *re, Str *input, Val *out, BOOL test_only, BOOL *matched);
int string_replace_regexp(Engine *e, Str *s, Obj *re, Val *repl, Val *ret);
int string_match_regexp(Engine *e, Str *s, Obj *re, Val *ret);
int string_split_regexp(Engine *e, Str *s, Obj *re, ULONG limit, Val *ret);
int string_search_regexp(Engine *e, Str *s, Obj *re, Val *ret);
void regexp_free(struct Regex *re);

/* ------------------------------------------------------------------ host.c */

int host_get(Engine *e, Obj *o, Str *key, Val *out, BOOL *found);
int host_put(Engine *e, Obj *o, Str *key, Val *v);
BOOL host_has(Engine *e, Obj *o, Str *key);
int host_delete(Engine *e, Obj *o, Str *key, BOOL *result);
int host_call(Engine *e, Obj *o, Val *self, Val *args, int argc, Val *ret, BOOL construct);
int host_put_args(Engine *e, Obj *o, Str *key, Val *args, int argc);
int host_call_member(Engine *e, Obj *o, Str *key, Val *args, int argc, Val *ret, BOOL *found, int ctx);
int host_get_quiet(Engine *e, Obj *o, Str *key, Val *out, BOOL *found);
Obj *script_object_of(Engine *e, IDispatch *d);
struct JsDisp *jsdisp_orphan(struct JsDisp *d);
int item_load(Engine *e, NamedItem *it);
int lookup_item_by_name(Engine *e, Str *name, Val *out, BOOL *found);
void init_globals_all(Engine *e);
void free_heap(Engine *e);
int atoi_local(const char *s);
int string_replace_string(Engine *e, Str *s, Str *search, Val *repl, Val *ret);
BOOL regexp_static_get(Engine *e, Str *key, Val *out);
BOOL regexp_static_put(Engine *e, Str *key, Val *v);
int host_default_value(Engine *e, Obj *o, int hint, Val *out);
int host_enum_keys(Engine *e, Obj *o, Obj *array);
Obj *host_wrap(Engine *e, IDispatch *d);
int variant_to_val(Engine *e, const VARIANT *v, Val *out);
int val_to_variant(Engine *e, Val *v, VARIANT *out);
IDispatch *dispatch_for_obj(Engine *e, Obj *o);
IDispatch *script_dispatch_create(Engine *e);
void host_release_obj(Engine *e, Obj *o);
void varbox_free(Engine *e, VarBox *b);
int enumerator_new(Engine *e, Val *args, int argc, Val *ret);
int enum_val_to_val(Engine *e, VARIANT *v, Val *out);
DISPID disp_id_of(Engine *e, Str *name);
Str *disp_name_of(Engine *e, DISPID id);
int fill_excepinfo_from_exception(Engine *e, EXCEPINFO *ei);
int host_item_dispatch_member(Engine *e, Str *name, Val *out, BOOL *found);

WCHAR *script_decode(const WCHAR *text, SIZE_T length, SIZE_T *result_length);

/* ------------------------------------------------------------------ script.c */

Engine *engine_create(void);
void engine_report(Engine *e);
HRESULT engine_enter(Engine *e);
void engine_note_code(Engine *e);
void engine_leave(Engine *e);
void engine_close(Engine *e);
int engine_run_pending(Engine *e);
void fault_clear(Engine *e);
void fault_from_exception(Engine *e);
void fault_from_syntax(Engine *e, HRESULT scode, ULONG line, ULONG col, const WCHAR *text_line, DWORD_PTR cookie);
extern LONG g_js_objects;
extern HINSTANCE g_js_module;

#endif
