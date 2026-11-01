/*
 * vbscript.dll internals: the syntax tree, the symbols and frames of a running script, and what the source files
 * share. a script is parsed into units of statements and procedures, then run by walking the tree.
 */
#ifndef MIME_VBS_H
#define MIME_VBS_H

#include <mime/win32/winbase.h>
#include <mime/win32/winuser.h>
#include <mime/win32/objbase.h>
#include <mime/win32/oleauto.h>
#include <mime/win32/activscp.h>
#include <rtl.h>

WINOLEAUTAPI HRESULT WINAPI CoGetObject(LPCWSTR name, void *options, REFIID riid, void **object);

double sin(double), cos(double), tan(double), atan(double), exp(double), log(double), sqrt(double), pow(double, double);
double floor(double), fmod(double, double), modf(double, double *);

#define VBS_ERR_BASE 0x800A0000u
#define VBS_LCID_DEFAULT 0x409

/* the numbers of the errors, which are also the low half of their hresult. */
enum {
    E_VBS_INVALID_CALL = 5, E_VBS_OVERFLOW = 6, E_VBS_OUT_OF_MEMORY = 7, E_VBS_SUBSCRIPT = 9, E_VBS_LOCKED = 10,
    E_VBS_DIV_ZERO = 11, E_VBS_TYPE_MISMATCH = 13, E_VBS_STACK = 28, E_VBS_NOT_SET = 91, E_VBS_NULL = 94,
    E_VBS_NOT_DEFINED = 35, E_VBS_OBJECT_REQUIRED = 424, E_VBS_CANNOT_CREATE = 429, E_VBS_NO_AUTOMATION = 430,
    E_VBS_NO_MEMBER = 438, E_VBS_WRONG_ARGS = 450, E_VBS_NOT_COLLECTION = 451, E_VBS_UNDEFINED = 500,
    E_VBS_ILLEGAL_ASSIGN = 501, E_VBS_CLASS_UNDEFINED = 506, E_VBS_NO_LOCALE = 447, E_VBS_PARAM_NOT_OPTIONAL = 449,
    E_VBS_NAMED_NOT_FOUND = 448, E_VBS_NO_NAMED = 446, E_VBS_BAD_VARTYPE = 458, E_VBS_UNQUALIFIED = 505,
    E_VBS_SYNTAX = 1002, E_VBS_UNKNOWN = 1000, E_VBS_OUT_OF_MEMORY_C = 1001
};

typedef struct Engine Engine;
typedef struct Expr Expr;
typedef struct Stmt Stmt;
typedef struct Proc Proc;
typedef struct Class Class;
typedef struct Unit Unit;
typedef struct Frame Frame;
typedef struct Sym Sym;
typedef struct Instance Instance;

/* ------------------------------------------------------------------ memory */

typedef struct Arena Arena;
void *vbs_alloc(SIZE_T size);
void *vbs_realloc(void *p, SIZE_T size);
void vbs_free(void *p);
PWSTR vbs_wcsdup(PCWSTR text);
PWSTR vbs_wcsndup(PCWSTR text, SIZE_T length);
void *arena_alloc(Arena *arena, SIZE_T size);
PWSTR arena_wcsndup(Arena *arena, PCWSTR text, SIZE_T length);
Arena *arena_create(void);
void arena_destroy(Arena *arena);

ULONG name_hash(PCWSTR name);
BOOL name_equal(PCWSTR a, PCWSTR b);

/* ------------------------------------------------------------------ tokens */

enum {
    T_EOF, T_NL, T_COLON, T_IDENT, T_STRING, T_INT, T_FLOAT, T_DATE, T_LPAREN, T_RPAREN, T_COMMA, T_DOT, T_EQ, T_NE,
    T_LT, T_GT, T_LE, T_GE, T_PLUS, T_MINUS, T_STAR, T_SLASH, T_BSLASH, T_CARET, T_AMP,
    K_AND, K_AS, K_BYREF, K_BYVAL, K_CALL, K_CASE, K_CLASS, K_CONST, K_DIM, K_DO, K_EACH, K_ELSE, K_ELSEIF, K_EMPTY,
    K_END, K_EQV, K_ERASE, K_EXIT, K_FALSE, K_FOR, K_FUNCTION, K_GOTO, K_IF, K_IMP, K_IN, K_IS, K_LET, K_LOOP, K_ME,
    K_MOD, K_NEW, K_NEXT, K_NOT, K_NOTHING, K_NULL, K_ON, K_OPTION, K_OR, K_PRIVATE, K_PROPERTY, K_PUBLIC, K_REDIM,
    K_SELECT, K_SET, K_SUB, K_THEN, K_TO, K_TRUE, K_UNTIL, K_WEND, K_WHILE, K_WITH, K_XOR, K_STEP, K_DEFAULT,
    K_LIKE, K_ERROR, K_EXPLICIT, K_PRESERVE, K_GET, K_RESUME, K_RANDOMIZE, K_EXECUTE, K_EXECUTEGLOBAL, K_STOP,
    K_RESERVED
};

/* ------------------------------------------------------------------ syntax tree */

enum {
    X_CONST, X_NAME, X_MEMBER, X_CALL, X_UNARY, X_BINARY, X_NEW, X_ME, X_EVAL
};

enum {
    OP_ADD, OP_SUB, OP_MUL, OP_DIV, OP_IDIV, OP_MOD, OP_POW, OP_CAT, OP_EQ, OP_NE, OP_LT, OP_GT, OP_LE, OP_GE, OP_IS,
    OP_AND, OP_OR, OP_XOR, OP_EQV, OP_IMP, OP_NEG, OP_PLUS, OP_NOT, OP_LIKE
};

/* how a name was bound when the unit was linked. */
enum { B_NONE, B_LOCAL, B_GLOBAL, B_FIELD, B_MEMBER, B_LATE, B_RESULT };

typedef struct Args {
    Expr **Items;
    UINT Count;
} Args;

struct Expr {
    BYTE Kind, Op, Bound, Parens;
    ULONG Line, Col;
    union {
        VARIANT Value;
        struct {
            PWSTR Name;
            ULONG Hash;
            LONG Slot;
            Sym *Symbol;
            ULONG Generation;
        } Name;
        struct {
            Expr *Object;
            PWSTR Name;
            Args Arguments;
            BOOL HasArguments;
            DISPID Cache;
        } Member;
        struct {
            Expr *Target;
            Args Arguments;
        } Call;
        struct {
            Expr *Left, *Right;
        } Operands;
        struct {
            PWSTR Name;
            Class *Cls;
        } New;
        Expr *Operand;
    } u;
};

enum {
    S_ASSIGN, S_SETASSIGN, S_CALL, S_IF, S_SELECT, S_FOR, S_FOREACH, S_DO, S_WHILE, S_WITH, S_EXIT, S_ONERROR, S_DIM,
    S_REDIM, S_ERASE, S_CONST, S_EXECUTE, S_RANDOMIZE, S_STOP, S_NOP, S_PROCDECL, S_CLASSDECL, S_OPTION
};

enum { EXIT_FOR, EXIT_DO, EXIT_SUB, EXIT_FUNCTION, EXIT_PROPERTY };

typedef struct CaseClause {
    Expr **Values;
    UINT Count;
    BOOL IsElse;
    Stmt *Body;
    struct CaseClause *Next;
} CaseClause;

typedef struct DimItem {
    PWSTR Name;
    Args Bounds;
    BOOL IsArray;
    Expr *Target;
    BYTE Public;
    struct DimItem *Next;
} DimItem;

struct Stmt {
    BYTE Kind, Flag, Flag2, Pad;
    ULONG Line, Col;
    Stmt *Next;
    union {
        struct {
            Expr *Target, *Value;
        } Assign;
        Expr *Call;
        struct {
            Expr *Condition;
            Stmt *Then, *Else;
        } If;
        struct {
            Expr *Subject;
            CaseClause *Cases;
        } Select;
        struct {
            Expr *Var, *From, *To, *Step;
            Stmt *Body;
        } For;
        struct {
            Expr *Var, *Group;
            Stmt *Body;
        } Each;
        struct {
            Expr *Condition;
            Stmt *Body;
        } Loop;
        struct {
            Expr *Object;
            Stmt *Body;
        } With;
        DimItem *Dim;
        struct {
            Expr *Target;
            Expr *Value;
            PWSTR Name;
        } Const;
        struct {
            Expr *Source;
            BOOL Global;
        } Execute;
        Expr *Randomize;
        Proc *Procedure;
        Class *Cls;
    } u;
};

typedef struct Param {
    PWSTR Name;
    BOOL ByVal;
} Param;

enum { PK_SUB, PK_FUNCTION, PK_GET, PK_LET, PK_SET };

struct Proc {
    PWSTR Name;
    BYTE Kind, Public, IsDefault, Linked;
    Param *Params;
    UINT ParamCount;
    Stmt *Body;
    /* locals: parameters first, then the result slot, then the declared and implied ones. */
    PWSTR *Locals;
    ULONG *LocalHashes;
    UINT LocalCount, LocalCapacity, DeclaredCount;
    LONG ResultSlot;
    Class *Cls;
    Unit *Unit;
    ULONG Line, Col;
    Proc *Next;
};

typedef struct Member {
    PWSTR Name;
    ULONG Hash;
    BYTE Kind, Public;
    LONG Slot;
    Proc *Get, *Let, *Set;
    Proc *Method;
    DISPID Id;
    struct Member *Next;
} Member;

enum { M_FIELD, M_METHOD, M_PROPERTY };

struct Class {
    PWSTR Name;
    Member *Members, *DefaultMember;
    UINT FieldCount, MemberCount;
    Proc *Initialize, *Terminate;
    Unit *Unit;
    ULONG Line, Col;
    Class *Next;
};

struct Unit {
    Arena *Memory;
    Stmt *Statements;
    Proc *Procedures;
    Class *Classes;
    PWSTR Source;
    ULONG FirstLine;
    DWORD_PTR Cookie;
    BOOL Linked, Global, Explicit;
    PWSTR *Names;
    UINT NameCount, NameCapacity;
    BSTR *Bstrs;
    UINT BstrCount, BstrCapacity;
    Unit *Next;
};

/* ------------------------------------------------------------------ symbols */

enum { SYM_VAR, SYM_CONST, SYM_PROC, SYM_CLASS, SYM_ITEM };

struct Sym {
    Sym *Next;
    PWSTR Name;
    ULONG Hash;
    BYTE Kind, Public, Declared;
    VARIANT Value;
    Proc *Procedure;
    Class *Cls;
    DWORD ItemFlags;
};

/* ------------------------------------------------------------------ running */

typedef struct WithLink {
    VARIANT Object;
    struct WithLink *Prev;
} WithLink;

typedef struct DynName {
    PWSTR Name;
    ULONG Hash;
    VARIANT Value;
    struct DynName *Next;
} DynName;

struct Frame {
    Engine *E;
    Proc *P;
    Instance *Me;
    VARIANT *Locals;
    UINT LocalCount;
    DynName *Dyn;
    BOOL ResumeNext;
    WithLink *With;
    Frame *Caller;
    ULONG Line, Col;
    Unit *Unit;
};

typedef struct ErrState {
    LONG Number;
    BSTR Description, Source, HelpFile;
    LONG HelpContext;
} ErrState;

/* what is told to the site about an error that ended a script. */
typedef struct Fault {
    BOOL Set;
    HRESULT Scode;
    BSTR Source, Description;
    ULONG Line, Col;
    BOOL Compile;
    PWSTR Text;
    DWORD_PTR Cookie;
    BOOL Host, Mapped;
} Fault;

typedef struct NamedItem {
    PWSTR Name;
    DWORD Flags;
    IDispatch *Object;
    struct NamedItem *Next;
} NamedItem;

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
    Sym *Buckets[128];
    Unit *Units;
    Unit *Pending;
    NamedItem *Items;
    ErrState Err;
    Fault Fault;
    IDispatch *ErrObject, *ScriptObject;
    ULONG Generation;
    UINT Depth;
    BOOL Explicit, Interrupted, Closing, InScript;
    DWORD Thread;
    double RndSeed;
    BOOL RndInit;
    Frame Global;
    VARIANT *GlobalValues;
    UINT NestedRun;
    HWND Window;
    DWORD InstanceCount;
};

/* instances of the script's classes, which are automation objects. */
struct Instance {
    IDispatch Dispatch;
    LONG Refs;
    Engine *E;
    Class *Cls;
    VARIANT *Fields;
    BOOL Terminated;
};

/* ------------------------------------------------------------------ functions of the sources */

/* parse.c */
Unit *parse_unit(Engine *e, PCWSTR text, ULONG first_line, DWORD_PTR cookie, BOOL expression, BOOL execute);
void unit_free(Unit *unit);
Expr *parse_expression_text(Engine *e, PCWSTR text, Unit **unit);

/* link.c */
BOOL unit_declare(Engine *e, Unit *unit, Frame *frame);
void unit_link(Engine *e, Unit *unit, Frame *frame);

/* run.c */
#define R_OK 0
#define R_ERR (-1)
#define R_EXIT_FOR 1
#define R_EXIT_DO 2
#define R_EXIT_SUB 3
#define R_EXIT_FUNCTION 4
#define R_EXIT_PROPERTY 5

int run_statements(Frame *frame, Stmt *stmt);
int eval(Frame *frame, Expr *expr, VARIANT *out);
int call_proc(Engine *e, Proc *proc, Instance *me, VARIANT *args, UINT count, VARIANT *result, Frame *caller);
int run_unit(Engine *e, Unit *unit, Frame *frame);
int eval_unit_expression(Engine *e, Expr *expr, Frame *frame, VARIANT *out);
Sym *sym_find(Engine *e, PCWSTR name, ULONG hash);
Sym *sym_add(Engine *e, PCWSTR name, int kind);
Member *member_find(Class *cls, PCWSTR name, ULONG hash);
void instance_destroy(Instance *inst);
Instance *instance_create(Engine *e, Class *cls, Frame *frame);
IDispatch *proc_ref_create(Engine *e, Proc *proc, Instance *me);

/* errors */
int raise_error(Frame *frame, LONG number, PCWSTR detail);
int raise_hresult(Frame *frame, HRESULT hr, PCWSTR source, PCWSTR description);
int raise_compile(Engine *e, LONG number, ULONG line, ULONG col, PCWSTR text, DWORD_PTR cookie);
void err_clear(Engine *e);
void err_set(Engine *e, LONG number, PCWSTR source, PCWSTR description);
PCWSTR error_text(LONG number);
LONG error_number_of_hresult(HRESULT hr);
HRESULT error_hresult(LONG number);
void fault_clear(Engine *e);
void describe_value(const VARIANT *v, WCHAR out[80]);

/* values (value.c) */
HRESULT var_binary(int op, VARIANT *l, VARIANT *r, VARIANT *out, LCID lcid);
HRESULT var_unary(int op, VARIANT *v, VARIANT *out, LCID lcid);
HRESULT var_compare(VARIANT *l, VARIANT *r, LCID lcid, int *result);
int value_of(Frame *frame, VARIANT *v);
void var_deref(VARIANT *dst, const VARIANT *src);
int to_bool(Frame *frame, VARIANT *v, BOOL *out);
int to_long(Frame *frame, VARIANT *v, LONG *out, PCWSTR detail);
HRESULT change_type(VARIANT *dst, VARIANT *src, VARTYPE vt, LCID lcid);
BSTR bstr_of_text(PCWSTR text, SIZE_T length);

/* objects (object.c) */
int obj_get(Frame *frame, VARIANT *object, PCWSTR name, Args *args, VARIANT *argv, UINT argc, WORD flags, VARIANT *result, DISPID *cache);
int obj_put(Frame *frame, VARIANT *object, PCWSTR name, VARIANT *argv, UINT argc, VARIANT *value, BOOL by_ref);
int obj_default(Frame *frame, VARIANT *object, WORD flags, VARIANT *argv, UINT argc, VARIANT *result);
int obj_enum(Frame *frame, VARIANT *collection, IEnumVARIANT **out);
IDispatch *err_object_create(Engine *e);
IDispatch *script_dispatch_create(Engine *e);
BOOL is_script_instance(IDispatch *d);
int create_object(Frame *frame, PCWSTR progid, VARIANT *out);

/* builtins (builtin.c) */
typedef int (*BUILTIN)(Frame *frame, VARIANT *args, UINT count, VARIANT *result);
typedef struct BuiltinDef {
    PCWSTR Name;
    BUILTIN Function;
    BYTE Min, Max, Raw;
} BuiltinDef;
const BuiltinDef *builtin_find(PCWSTR name, ULONG hash);
BOOL constant_find(PCWSTR name, VARIANT *out);
int builtin_eval(Frame *frame, VARIANT *source, VARIANT *result, BOOL execute, BOOL global);

/* regexp.c */
IDispatch *regexp_create(Engine *e);
PCWSTR regexp_typename(IDispatch *object);

/* script.c */
Engine *engine_create(void);
int engine_run_global(Engine *e);
void engine_report(Engine *e, BOOL terminate);
HRESULT engine_enter(Engine *e);
void engine_leave(Engine *e);
void engine_close(Engine *e);
extern LONG g_vbs_objects;
extern HINSTANCE g_vbs_module;

#endif
