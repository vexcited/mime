#ifndef MIME_LIB_HLSL_INTERNAL_H
#define MIME_LIB_HLSL_INTERNAL_H

#include <stdarg.h>
#include <stdbool.h>

#include "hlsl.h"

#define HL_ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

typedef struct HlBuf {
    char *data;
    size_t length, capacity;
} HlBuf;

/* a place in the source: errors give file(line,col) or file(line,col-end). */
typedef struct HlLoc {
    uint32_t file;
    uint32_t line;
    uint32_t col;
} HlLoc;

typedef struct HlFile {
    const char *name;
    const char *data;
    uint32_t size;
    /* the data ID3DInclude::Open gave, which children get as their parent; NULL for the source. */
    const void *opened;
    uint32_t parent;
    bool once;
} HlFile;

typedef struct HlChunk HlChunk;

typedef struct HlWarningState {
    uint32_t code;
    /* 0 default, 1 disabled, 2 error, 3 once. */
    uint8_t mode;
} HlWarningState;

typedef struct HlToken HlToken;
typedef struct HlProgram HlProgram;
typedef struct HlType HlType;

struct Hlsl {
    MimeAllocator alloc;
    void (*fatal)(void *ctx);
    void *fatal_ctx;
    HlChunk *chunks;
    uint8_t *at, *end;

    const char **interned;
    uint32_t intern_count, intern_capacity;

    HlBuf messages;
    HlBuf output;
    uint32_t errors;
    bool warnings_are_errors;
    HlWarningState *warnings;
    uint32_t warning_count, warning_capacity;

    const HlslOptions *options;
    const HlslInclude *include;
    HlFile *files;
    uint32_t file_count, file_capacity;
    /* the names errors print for the source, as fxc does for unnamed sources. */
    const char *source_name;

    HlType **type_cache;
    HlType *void_type, *error_type, *string_type;

    /* the preprocessed tokens, as an array the parser reads. */
    HlToken **tokens;
    uint32_t token_count;
};

[[noreturn]] void hl_fatal(Hlsl *h);
/* zeroed; never NULL. */
void *hl_alloc(Hlsl *h, size_t size);
void *hl_realloc(Hlsl *h, void *p, size_t old_size, size_t size);
char *hl_strndup(Hlsl *h, const char *s, size_t n);
const char *hl_intern(Hlsl *h, const char *s, size_t n);
static inline const char *hl_intern_z(Hlsl *h, const char *s) { return hl_intern(h, s, __builtin_strlen(s)); }

#define HL_GROW(h, items, count, capacity)                                                                         \
    do {                                                                                                           \
        if ((count) >= (capacity)) {                                                                               \
            uint32_t grown_ = (capacity) ? (capacity) * 2 : 8;                                                     \
            (items) = hl_realloc((h), (items), (size_t)(capacity) * sizeof(*(items)), (size_t)grown_ * sizeof(*(items))); \
            (capacity) = grown_;                                                                                   \
        }                                                                                                          \
    } while (0)

#define HL_PUSH(h, items, count, capacity, value)                                                                  \
    do {                                                                                                           \
        HL_GROW(h, items, count, capacity);                                                                        \
        (items)[(count)++] = (value);                                                                              \
    } while (0)

void hl_buf_append(Hlsl *h, HlBuf *b, const char *s, size_t n);
void hl_buf_puts(Hlsl *h, HlBuf *b, const char *s);
void hl_buf_putc(Hlsl *h, HlBuf *b, char c);
/* %s %.*s %c %d %u %x %X %ld %lu %lld %llu %p %f %g with widths, '-' and '0'. */
void hl_buf_printf(Hlsl *h, HlBuf *b, const char *format, ...);
void hl_buf_vprintf(Hlsl *h, HlBuf *b, const char *format, va_list ap);
/* nul terminated, in the arena. */
char *hl_sprintf(Hlsl *h, const char *format, ...);
/* shortest text that reads back as the same float, as fxc prints literals. */
void hl_format_float(char *out, size_t size, float value);

void hl_error(Hlsl *h, HlLoc loc, uint32_t code, const char *format, ...);
void hl_warning(Hlsl *h, HlLoc loc, uint32_t code, const char *format, ...);
/* a note after an error, as "see declaration of 'x'". */
void hl_note(Hlsl *h, HlLoc loc, const char *format, ...);
void hl_set_warning(Hlsl *h, uint32_t code, uint8_t mode);

static inline bool hl_streq(const char *a, const char *b) { return __builtin_strcmp(a, b) == 0; }
int hl_stricmp(const char *a, const char *b);
int hl_strnicmp(const char *a, const char *b, size_t n);

/* ------------------------------------------------------------------ tokens */

enum {
    TK_EOF,
    TK_IDENT,
    TK_NUMBER,
    TK_STRING,
    TK_CHAR,
    TK_PUNCT,
    /* a #pragma the compiler reads, its text in the token. */
    TK_PRAGMA,
    /* the end of an included file, which the preprocessor drops. */
    TK_FILE_END,
    /* placemarker for empty macro arguments next to ##. */
    TK_PLACEMARKER,
};

enum {
    TF_SPACE = 1,
    TF_BOL = 2,
};

/* punctuators, from the longest. */
enum {
    P_NONE,
    P_ELLIPSIS,
    P_SHL_ASSIGN,
    P_SHR_ASSIGN,
    P_ARROW,
    P_INC,
    P_DEC,
    P_SHL,
    P_SHR,
    P_LE,
    P_GE,
    P_EQ,
    P_NE,
    P_ANDAND,
    P_OROR,
    P_MUL_ASSIGN,
    P_DIV_ASSIGN,
    P_MOD_ASSIGN,
    P_ADD_ASSIGN,
    P_SUB_ASSIGN,
    P_AND_ASSIGN,
    P_XOR_ASSIGN,
    P_OR_ASSIGN,
    P_HASHHASH,
    P_COLONCOLON,
    P_SINGLE,
};

struct HlHideset;

struct HlToken {
    HlToken *next;
    const char *text;
    uint32_t length;
    uint8_t kind;
    uint8_t flags;
    /* a P_* value, or the character of single character punctuators. */
    uint16_t punct;
    HlLoc loc;
    /* identifiers, interned. */
    const char *ident;
    struct HlHideset *hide;
    /* the tokens of a TK_PRAGMA after "#pragma". */
    HlToken *pragma;
};

static inline bool hl_is_punct(const HlToken *t, int p) { return t->kind == TK_PUNCT && t->punct == p; }
static inline bool hl_is_ident(const HlToken *t, const char *interned) { return t->kind == TK_IDENT && t->ident == interned; }

/* fills h->tokens; 0 when there was no error. */
int hl_preprocess(Hlsl *h);
/* the preprocessed source as D3DPreprocess returns it. */
void hl_print_tokens(Hlsl *h, HlBuf *out);
const char *hl_token_text(Hlsl *h, const HlToken *t);
const char *hl_file_name(Hlsl *h, uint32_t file);

/* parses, checks and compiles h->tokens into h->output. */
int hl_compile(Hlsl *h);

/* ------------------------------------------------------------------ types */

enum {
    TY_ERROR,
    TY_VOID,
    TY_SCALAR,
    TY_VECTOR,
    TY_MATRIX,
    TY_ARRAY,
    TY_STRUCT,
    TY_OBJECT,
    TY_STRING,
};

/* scalar types, in the order binary operators promote them. */
enum {
    BT_BOOL,
    BT_LIT_INT,
    BT_MIN12INT,
    BT_MIN16INT,
    BT_INT,
    BT_MIN16UINT,
    BT_UINT,
    BT_LIT_FLOAT,
    BT_MIN10FLOAT,
    BT_MIN16FLOAT,
    BT_HALF,
    BT_FLOAT,
    BT_DOUBLE,
    BT_COUNT,
};

enum {
    OBJ_NONE,
    OBJ_SAMPLER,
    OBJ_SAMPLER_COMPARISON,
    /* shader model 2 and 3 names; sampler1D and the others are samplers, texture a texture of any kind. */
    OBJ_SAMPLER1D,
    OBJ_SAMPLER2D,
    OBJ_SAMPLER3D,
    OBJ_SAMPLERCUBE,
    OBJ_TEXTURE,
    OBJ_BUFFER,
    OBJ_TEXTURE1D,
    OBJ_TEXTURE1DARRAY,
    OBJ_TEXTURE2D,
    OBJ_TEXTURE2DARRAY,
    OBJ_TEXTURE2DMS,
    OBJ_TEXTURE2DMSARRAY,
    OBJ_TEXTURE3D,
    OBJ_TEXTURECUBE,
    OBJ_TEXTURECUBEARRAY,
    OBJ_STRUCTURED_BUFFER,
    OBJ_BYTE_ADDRESS_BUFFER,
    OBJ_RWBUFFER,
    OBJ_RWTEXTURE1D,
    OBJ_RWTEXTURE1DARRAY,
    OBJ_RWTEXTURE2D,
    OBJ_RWTEXTURE2DARRAY,
    OBJ_RWTEXTURE3D,
    OBJ_RWSTRUCTURED_BUFFER,
    OBJ_RWBYTE_ADDRESS_BUFFER,
    OBJ_APPEND_STRUCTURED_BUFFER,
    OBJ_CONSUME_STRUCTURED_BUFFER,
    OBJ_INPUT_PATCH,
    OBJ_OUTPUT_PATCH,
    OBJ_POINT_STREAM,
    OBJ_LINE_STREAM,
    OBJ_TRIANGLE_STREAM,
    OBJ_CONSTANT_BUFFER,
    OBJ_TEXTURE_BUFFER,
    /* effect objects, as blend states and compiled shaders: parsed and ignored. */
    OBJ_STATE,
    OBJ_COUNT,
};

enum {
    TM_ROW_MAJOR = 1,
    TM_COLUMN_MAJOR = 2,
    /* rasterizer ordered views are the read-write objects with this. */
    TM_ROV = 4,
    TM_SNORM = 8,
    TM_UNORM = 16,
    TM_GLOBALLY_COHERENT = 32,
};

typedef struct HlStruct HlStruct;
typedef struct HlExpr HlExpr;
typedef struct HlStmt HlStmt;
typedef struct HlFunc HlFunc;
typedef struct HlVar HlVar;
typedef struct HlBuffer HlBuffer;

struct HlType {
    uint8_t kind;
    uint8_t base;
    /* vectors are 1 row; scalars 1 by 1. */
    uint8_t rows, cols;
    uint8_t modifiers;
    uint8_t object;
    /* arrays: 0 for an unsized array. */
    uint32_t count;
    /* the element of arrays, and the template argument of objects. */
    HlType *elem;
    HlStruct *st;
    /* Texture2DMS<T, samples>, InputPatch<T, points>. */
    uint32_t samples;
    /* the name of typedefs, for messages. */
    const char *name;
};

typedef struct HlField {
    const char *name;
    HlType *type;
    const char *semantic;
    uint32_t interp;
    HlLoc loc;
} HlField;

struct HlStruct {
    const char *name;
    HlField *fields;
    uint32_t field_count, field_capacity;
    HlLoc loc;
    bool interface;
    bool klass;
    HlStruct *base;
    HlFunc **methods;
    uint32_t method_count, method_capacity;
};

/* ------------------------------------------------------------------ constants */

typedef union HlValue {
    uint32_t u;
    int32_t i;
    float f;
} HlValue;

/* ------------------------------------------------------------------ expressions */

enum {
    E_ERROR,
    E_CONST,
    E_VAR,
    E_UNARY,
    E_BINARY,
    E_ASSIGN,
    E_TERNARY,
    E_CAST,
    E_INDEX,
    E_FIELD,
    E_SWIZZLE,
    E_CALL,
    E_INTRINSIC,
    E_METHOD,
    E_CONSTRUCT,
    E_INIT_LIST,
    E_COMMA,
    /* a function or method name before its call. */
    E_FUNCTION_NAME,
    /* tex.mips[level], tex.sample[index] and the like, before their own index. */
    E_OBJECT_PROPERTY,
};

/* unary and binary operators; assignments give theirs, or 0 for a plain one. */
enum {
    OPR_NONE,
    OPR_NEG,
    OPR_PLUS,
    OPR_NOT,
    OPR_BITNOT,
    OPR_PREINC,
    OPR_PREDEC,
    OPR_POSTINC,
    OPR_POSTDEC,
    OPR_ADD,
    OPR_SUB,
    OPR_MUL,
    OPR_DIV,
    OPR_MOD,
    OPR_SHL,
    OPR_SHR,
    OPR_LT,
    OPR_GT,
    OPR_LE,
    OPR_GE,
    OPR_EQ,
    OPR_NE,
    OPR_BITAND,
    OPR_BITXOR,
    OPR_BITOR,
    OPR_AND,
    OPR_OR,
};

struct HlExpr {
    uint8_t kind;
    uint8_t op;
    /* the expression names something that can be assigned. */
    bool lvalue;
    /* E_CAST: written in the source, rather than made by the compiler. */
    bool explicit_cast;
    HlType *type;
    HlLoc loc;
    HlExpr *a, *b, *c;
    HlExpr **args;
    uint32_t arg_count;
    HlVar *var;
    HlFunc *func;
    /* E_INTRINSIC and E_METHOD: the intrinsic; E_OBJECT_PROPERTY: which. */
    uint32_t intrinsic;
    /* E_CONST: one value per component, of the type's base. */
    HlValue *values;
    /* E_SWIZZLE: flat components of a; E_FIELD: the field index. */
    uint8_t swizzle[16];
    uint32_t field;
};

/* ------------------------------------------------------------------ declarations */

enum {
    ST_STATIC = 1 << 0,
    ST_UNIFORM = 1 << 1,
    ST_EXTERN = 1 << 2,
    ST_SHARED = 1 << 3,
    ST_GROUPSHARED = 1 << 4,
    ST_CONST = 1 << 5,
    ST_VOLATILE = 1 << 6,
    ST_PRECISE = 1 << 7,
    ST_IN = 1 << 8,
    ST_OUT = 1 << 9,
    ST_INLINE = 1 << 10,
    /* geometry shader input primitives. */
    ST_POINT = 1 << 11,
    ST_LINE = 1 << 12,
    ST_TRIANGLE = 1 << 13,
    ST_LINEADJ = 1 << 14,
    ST_TRIANGLEADJ = 1 << 15,
    /* hull and domain shader patch parameters. */
    ST_INDICES = 1 << 16,
    ST_VERTICES = 1 << 17,
    ST_PRIMITIVES = 1 << 18,
    ST_PAYLOAD = 1 << 19,
};

/* interpolation modifiers. */
enum {
    IM_NOINTERPOLATION = 1,
    IM_LINEAR = 2,
    IM_CENTROID = 4,
    IM_NOPERSPECTIVE = 8,
    IM_SAMPLE = 16,
};

/* register(x#, space#) as given, per profile when one is named. */
typedef struct HlRegister {
    char kind;
    uint32_t index;
    uint32_t space;
    bool has_space;
    const char *profile;
    HlLoc loc;
} HlRegister;

struct HlVar {
    const char *name;
    HlType *type;
    HlLoc loc;
    uint32_t storage;
    uint32_t interp;
    const char *semantic;
    HlRegister *registers;
    uint32_t register_count;
    bool has_packoffset;
    uint32_t packoffset;
    HlExpr *init;
    bool global;
    bool param;
    /* a uniform in a cbuffer, $Globals included. */
    HlBuffer *buffer;
    /* written to somewhere, read from somewhere. */
    bool assigned, used;
    /* an index that is not a constant reads it: it lives in an indexable array. */
    bool dynamic_index;
    /* the lowering's data. */
    void *lower;
    uint32_t id;
    /* a field of a method's this, named alone. */
    HlVar *this_var;
    uint32_t this_field;
};

struct HlBuffer {
    const char *name;
    bool tbuffer;
    HlLoc loc;
    HlRegister *registers;
    uint32_t register_count;
    HlVar **vars;
    uint32_t var_count, var_capacity;
    /* ConstantBuffer<T>: the struct and the variable naming the buffer. */
    HlVar *object;
    bool used;
    uint32_t size;
    uint32_t bind;
    uint32_t space;
};

typedef struct HlAttribute {
    const char *name;
    HlExpr **args;
    uint32_t arg_count;
    HlLoc loc;
} HlAttribute;

struct HlFunc {
    const char *name;
    HlType *ret;
    HlVar **params;
    uint32_t param_count;
    HlStmt *body;
    HlLoc loc;
    const char *semantic;
    uint32_t ret_interp;
    HlAttribute *attrs;
    uint32_t attr_count;
    /* the declaration that a definition defines. */
    HlFunc *decl;
    HlFunc *definition;
    HlStruct *owner;
    /* methods: the object they run on. */
    HlVar *this_var;
    uint32_t storage;
    /* calls in progress, against recursion. */
    uint32_t active;
};

/* ------------------------------------------------------------------ statements */

enum {
    S_EMPTY,
    S_BLOCK,
    S_EXPR,
    S_DECL,
    S_IF,
    S_FOR,
    S_WHILE,
    S_DO,
    S_SWITCH,
    S_CASE,
    S_DEFAULT,
    S_BREAK,
    S_CONTINUE,
    S_RETURN,
    S_DISCARD,
};

struct HlStmt {
    uint8_t kind;
    HlLoc loc;
    HlStmt **stmts;
    uint32_t stmt_count;
    HlExpr *expr;
    HlExpr *cond;
    HlExpr *iter;
    HlStmt *init;
    HlStmt *body;
    HlStmt *other;
    HlVar **vars;
    uint32_t var_count;
    HlAttribute *attrs;
    uint32_t attr_count;
    /* S_CASE. */
    HlValue value;
};

/* ------------------------------------------------------------------ the program */

enum { STAGE_PIXEL, STAGE_VERTEX, STAGE_GEOMETRY, STAGE_HULL, STAGE_DOMAIN, STAGE_COMPUTE, STAGE_LIBRARY, STAGE_EFFECT };

typedef struct HlProfile {
    uint8_t stage;
    uint8_t major, minor;
    /* ps_4_0_level_9_1 and the like: 91, 93, or 0. */
    uint8_t level9;
    const char *name;
} HlProfile;

struct HlProgram {
    HlProfile profile;
    HlFunc **funcs;
    uint32_t func_count, func_capacity;
    HlVar **globals;
    uint32_t global_count, global_capacity;
    HlBuffer **buffers;
    uint32_t buffer_count, buffer_capacity;
    HlBuffer *globals_buffer;
    HlStruct **structs;
    uint32_t struct_count, struct_capacity;
    uint32_t var_ids;
};

/* ------------------------------------------------------------------ type helpers */

HlType *hl_type_error(Hlsl *h);
HlType *hl_type_void(Hlsl *h);
HlType *hl_type_string(Hlsl *h);
HlType *hl_type_numeric(Hlsl *h, uint8_t base, uint8_t rows, uint8_t cols, uint8_t kind);
static inline HlType *hl_type_scalar(Hlsl *h, uint8_t base) { return hl_type_numeric(h, base, 1, 1, TY_SCALAR); }
static inline HlType *hl_type_vector(Hlsl *h, uint8_t base, uint8_t n)
{
    return hl_type_numeric(h, base, 1, n, TY_VECTOR);
}
static inline HlType *hl_type_matrix(Hlsl *h, uint8_t base, uint8_t r, uint8_t c)
{
    return hl_type_numeric(h, base, r, c, TY_MATRIX);
}
HlType *hl_type_majored(Hlsl *h, HlType *t, uint8_t modifiers);
HlType *hl_type_array(Hlsl *h, HlType *elem, uint32_t count);
HlType *hl_type_object(Hlsl *h, uint8_t object, HlType *elem, uint32_t samples);
HlType *hl_type_struct(Hlsl *h, HlStruct *st);
/* the same shape with another scalar type. */
HlType *hl_type_with_base(Hlsl *h, const HlType *t, uint8_t base);
bool hl_type_equal(const HlType *a, const HlType *b);
static inline bool hl_type_numeric_p(const HlType *t)
{
    return t->kind == TY_SCALAR || t->kind == TY_VECTOR || t->kind == TY_MATRIX;
}
/* scalars in the flattened type: structs field by field, arrays element by element, matrices row by row. */
uint32_t hl_type_components(const HlType *t);
bool hl_type_has_object(const HlType *t);
const char *hl_type_name(Hlsl *h, const HlType *t);
const char *hl_base_name(uint8_t base);
bool hl_base_float(uint8_t base);
bool hl_base_int(uint8_t base);
bool hl_base_unsigned(uint8_t base);
/* the 32-bit type the base computes in: BT_FLOAT, BT_INT, BT_UINT, BT_BOOL or BT_DOUBLE. */
uint8_t hl_base_storage(uint8_t base);
/* the scalar type of a flattened component. */
uint8_t hl_type_component_base(const HlType *t, uint32_t index);
/* registers and bytes in a constant buffer, with row or column major matrices. */
uint32_t hl_type_cb_size(const HlType *t, bool row_major_default);
bool hl_type_row_major(const HlType *t, bool row_major_default);
/* object kinds. */
bool hl_object_is_texture(uint8_t object);
bool hl_object_is_uav(uint8_t object);
bool hl_object_is_sampler(uint8_t object);
/* the coordinate count for loads and samples; dims of the texture without array index. */
uint32_t hl_object_dims(uint8_t object);
bool hl_object_is_array(uint8_t object);
const char *hl_object_name(uint8_t object);

/* ------------------------------------------------------------------ intrinsics */

enum {
    I_NONE,
    I_ABS,
    I_ACOS,
    I_ALL,
    I_ANY,
    I_ASDOUBLE,
    I_ASFLOAT,
    I_ASIN,
    I_ASINT,
    I_ASUINT,
    I_ATAN,
    I_ATAN2,
    I_CEIL,
    I_CLAMP,
    I_CLIP,
    I_COS,
    I_COSH,
    I_COUNTBITS,
    I_CROSS,
    I_DDX,
    I_DDX_COARSE,
    I_DDX_FINE,
    I_DDY,
    I_DDY_COARSE,
    I_DDY_FINE,
    I_DEGREES,
    I_DETERMINANT,
    I_DISTANCE,
    I_DOT,
    I_DST,
    I_EVAL_CENTROID,
    I_EVAL_SAMPLE,
    I_EVAL_SNAPPED,
    I_EXP,
    I_EXP2,
    I_F16TOF32,
    I_F32TOF16,
    I_FACEFORWARD,
    I_FIRSTBITHIGH,
    I_FIRSTBITLOW,
    I_FLOOR,
    I_FMA,
    I_FMOD,
    I_FRAC,
    I_FREXP,
    I_FWIDTH,
    I_RT_SAMPLE_COUNT,
    I_RT_SAMPLE_POSITION,
    I_GROUP_BARRIER,
    I_GROUP_BARRIER_SYNC,
    I_DEVICE_BARRIER,
    I_DEVICE_BARRIER_SYNC,
    I_ALL_BARRIER,
    I_ALL_BARRIER_SYNC,
    I_INTERLOCKED_ADD,
    I_INTERLOCKED_AND,
    I_INTERLOCKED_OR,
    I_INTERLOCKED_XOR,
    I_INTERLOCKED_MIN,
    I_INTERLOCKED_MAX,
    I_INTERLOCKED_EXCHANGE,
    I_INTERLOCKED_COMPARE_EXCHANGE,
    I_INTERLOCKED_COMPARE_STORE,
    I_ISFINITE,
    I_ISINF,
    I_ISNAN,
    I_LDEXP,
    I_LENGTH,
    I_LERP,
    I_LIT,
    I_LOG,
    I_LOG10,
    I_LOG2,
    I_MAD,
    I_MAX,
    I_MIN,
    I_MODF,
    I_MSAD4,
    I_MUL,
    I_NOISE,
    I_NORMALIZE,
    I_POW,
    I_RADIANS,
    I_RCP,
    I_REFLECT,
    I_REFRACT,
    I_REVERSEBITS,
    I_ROUND,
    I_RSQRT,
    I_SATURATE,
    I_SIGN,
    I_SIN,
    I_SINCOS,
    I_SINH,
    I_SMOOTHSTEP,
    I_SQRT,
    I_STEP,
    I_TAN,
    I_TANH,
    I_TRANSPOSE,
    I_TRUNC,
    I_TEX1D,
    I_TEX1DBIAS,
    I_TEX1DGRAD,
    I_TEX1DLOD,
    I_TEX1DPROJ,
    I_TEX2D,
    I_TEX2DBIAS,
    I_TEX2DGRAD,
    I_TEX2DLOD,
    I_TEX2DPROJ,
    I_TEX3D,
    I_TEX3DBIAS,
    I_TEX3DGRAD,
    I_TEX3DLOD,
    I_TEX3DPROJ,
    I_TEXCUBE,
    I_TEXCUBEBIAS,
    I_TEXCUBEGRAD,
    I_TEXCUBELOD,
    I_TEXCUBEPROJ,
    I_ABORT,
    I_ERRORF,
    I_PRINTF,
    I_CHECK_ACCESS,
    I_D3DCOLORTOUBYTE4,
    I_TESS_TRI_EDGE,
    I_TESS_TRI_AVG,
    I_TESS_TRI_SINGLE,
    I_TESS_QUAD_EDGE,
    I_TESS_QUAD_AVG,
    I_TESS_QUAD_SINGLE,
    I_TESS_QUAD_2D_EDGE,
    I_TESS_QUAD_2D_AVG,
    I_TESS_QUAD_2D_SINGLE,
    I_TESS_ISOLINE,
    I_COUNT,

    /* methods of objects. */
    M_SAMPLE = 0x100,
    M_SAMPLE_BIAS,
    M_SAMPLE_GRAD,
    M_SAMPLE_LEVEL,
    M_SAMPLE_CMP,
    M_SAMPLE_CMP_LEVEL_ZERO,
    M_LOAD,
    M_GATHER,
    M_GATHER_RED,
    M_GATHER_GREEN,
    M_GATHER_BLUE,
    M_GATHER_ALPHA,
    M_GATHER_CMP,
    M_GATHER_CMP_RED,
    M_GATHER_CMP_GREEN,
    M_GATHER_CMP_BLUE,
    M_GATHER_CMP_ALPHA,
    M_GET_DIMENSIONS,
    M_CALCULATE_LOD,
    M_CALCULATE_LOD_UNCLAMPED,
    M_GET_SAMPLE_POSITION,
    M_LOAD2,
    M_LOAD3,
    M_LOAD4,
    M_STORE,
    M_STORE2,
    M_STORE3,
    M_STORE4,
    M_INTERLOCKED_ADD,
    M_INTERLOCKED_AND,
    M_INTERLOCKED_OR,
    M_INTERLOCKED_XOR,
    M_INTERLOCKED_MIN,
    M_INTERLOCKED_MAX,
    M_INTERLOCKED_EXCHANGE,
    M_INTERLOCKED_COMPARE_EXCHANGE,
    M_INTERLOCKED_COMPARE_STORE,
    M_APPEND,
    M_CONSUME,
    M_INCREMENT_COUNTER,
    M_DECREMENT_COUNTER,
    M_RESTART_STRIP,

    /* tex.mips and tex.sample. */
    PROP_MIPS = 0x200,
    PROP_SAMPLE,
};

#endif
