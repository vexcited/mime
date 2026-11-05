#ifndef MIME_LIB_HLSL_PARSE_H
#define MIME_LIB_HLSL_PARSE_H

#include "internal.h"

enum { SYM_VAR, SYM_TYPE, SYM_FUNC, SYM_STRUCT };

typedef struct Symbol Symbol;
struct Symbol {
    const char *name;
    uint8_t kind;
    uint32_t depth;
    Symbol *shadowed;
    Symbol *scope_next;
    HlVar *var;
    HlType *type;
    HlFunc **funcs;
    uint32_t func_count, func_capacity;
};

typedef struct Bucket Bucket;
struct Bucket {
    const char *name;
    Symbol *sym;
    Bucket *next;
};

typedef struct Parser {
    Hlsl *h;
    HlProgram *prog;
    HlToken **tokens;
    uint32_t pos, count;
    Bucket **buckets;
    uint32_t bucket_count;
    Symbol *scope_syms;
    uint32_t depth;
    /* #pragma pack_matrix and the compile flags: TM_ROW_MAJOR or TM_COLUMN_MAJOR. */
    uint8_t pack_default;
    HlFunc *func;
    HlStruct *klass;
    uint32_t loops, switches;
    uint32_t nesting;
    /* identifiers the grammar knows, interned. */
    struct {
        const char *k_struct, *k_class, *k_interface, *k_cbuffer, *k_tbuffer, *k_typedef, *k_static, *k_const,
            *k_uniform, *k_extern, *k_shared, *k_groupshared, *k_volatile, *k_precise, *k_inline, *k_in, *k_out,
            *k_inout, *k_nointerpolation, *k_linear, *k_centroid, *k_noperspective, *k_sample, *k_row_major,
            *k_column_major, *k_snorm, *k_unorm, *k_globallycoherent, *k_if, *k_else, *k_for, *k_while, *k_do,
            *k_switch, *k_case, *k_default, *k_break, *k_continue, *k_return, *k_discard, *k_true, *k_false,
            *k_register, *k_packoffset, *k_technique, *k_technique10, *k_technique11, *k_pass, *k_compile,
            *k_sampler_state, *k_point, *k_line, *k_triangle, *k_lineadj, *k_triangleadj, *k_vector, *k_matrix,
            *k_void, *k_string, *k_unsigned, *k_fxgroup, *k_stateblock_state, *k_compile_fragment, *k_indices,
            *k_vertices, *k_primitives, *k_payload, *k_namespace, *k_NULL, *k_this, *k_sizeof;
    } k;
} Parser;

/* ------------------------------------------------------------------ tokens */

static inline HlToken *peek(Parser *p) { return p->tokens[p->pos < p->count ? p->pos : p->count - 1]; }
static inline HlToken *peek_at(Parser *p, uint32_t n)
{
    uint32_t at = p->pos + n;
    return p->tokens[at < p->count ? at : p->count - 1];
}
static inline HlToken *next(Parser *p)
{
    HlToken *t = peek(p);
    if (p->pos < p->count - 1)
        p->pos++;
    return t;
}
static inline bool at_punct(Parser *p, int c) { return hl_is_punct(peek(p), c); }
static inline bool at_ident(Parser *p, const char *k) { return hl_is_ident(peek(p), k); }
static inline bool accept_punct(Parser *p, int c)
{
    if (!at_punct(p, c))
        return false;
    next(p);
    return true;
}
static inline bool accept_ident(Parser *p, const char *k)
{
    if (!at_ident(p, k))
        return false;
    next(p);
    return true;
}
bool expect_punct(Parser *p, int c);
void syntax_error(Parser *p, HlToken *t);
/* skips to a ';' or '}' of the level, after an error. */
void recover(Parser *p);

/* ------------------------------------------------------------------ symbols */

Symbol *lookup(Parser *p, const char *name);
Symbol *declare(Parser *p, const char *name, uint8_t kind);
void push_scope(Parser *p);
void pop_scope(Parser *p);

/* a type at the cursor, or NULL; reads template arguments. */
HlType *parse_type(Parser *p, bool required);
bool at_type(Parser *p);
/* the built in type an identifier names, as float4x3 and Texture2D, with no template arguments read. */
HlType *builtin_type(Parser *p, const char *name, uint8_t *object);

/* ------------------------------------------------------------------ expressions */

HlExpr *parse_expr(Parser *p);
HlExpr *parse_assignment(Parser *p);
HlExpr *parse_template_arg(Parser *p);
HlExpr *new_expr(Parser *p, uint8_t kind, HlType *type, HlLoc loc);
HlExpr *error_expr(Parser *p, HlLoc loc);
HlExpr *const_int(Parser *p, uint8_t base, int32_t v, HlLoc loc);
HlExpr *const_float(Parser *p, float v, HlLoc loc);
/* an implicit or explicit conversion; reports what cannot be converted. */
HlExpr *convert(Parser *p, HlExpr *e, HlType *to, HlLoc loc, bool explicit_cast);
bool can_convert(Parser *p, const HlType *from, const HlType *to, bool explicit_cast);
/* folds an expression of constants, or returns it. */
HlExpr *fold(Parser *p, HlExpr *e);
/* the integer value of a constant expression, false with an error otherwise. */
bool const_uint(Parser *p, HlExpr *e, uint32_t *out, const char *what);
HlExpr *init_list(Parser *p, HlLoc loc);
/* checks an initializer against the declared type; NULL types array sizes from it. */
HlExpr *check_initializer(Parser *p, HlExpr *init, HlType **type, HlLoc loc);
HlExpr *var_expr(Parser *p, HlVar *v, HlLoc loc);
/* marks a variable an expression assigns, as out arguments do. */
void mark_assigned(Parser *p, HlExpr *e);
/* the common type binary arithmetic gives two types, with truncation warnings. */
HlType *arith_type(Parser *p, HlType *a, HlType *b, HlLoc loc, bool warn);

/* ------------------------------------------------------------------ intrinsics */

/* the intrinsic of the name, or I_NONE. */
uint32_t intrinsic_lookup(Parser *p, const char *name);
HlExpr *check_intrinsic(Parser *p, uint32_t id, HlExpr **args, uint32_t count, HlLoc loc, const char *name);
uint32_t method_lookup(Parser *p, const HlType *object, const char *name);
HlExpr *check_method(Parser *p, HlExpr *object, uint32_t id, HlExpr **args, uint32_t count, HlLoc loc,
                     const char *name);
/* user functions: the best overload for the arguments, with conversions applied. */
HlExpr *resolve_call(Parser *p, Symbol *s, HlExpr **args, uint32_t count, HlLoc loc, HlExpr *object);
uint32_t conversion_cost(Parser *p, const HlType *from, const HlType *to);

/* ------------------------------------------------------------------ declarations */

void parse_program(Parser *p);
HlStmt *parse_block(Parser *p);
void parse_attributes(Parser *p, HlAttribute **attrs, uint32_t *count);

#endif
