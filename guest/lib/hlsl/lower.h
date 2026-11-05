#ifndef MIME_LIB_HLSL_LOWER_H
#define MIME_LIB_HLSL_LOWER_H

#include "ir.h"

enum {
    VK_NONE,
    /* scalars in virtual registers, a home register per component. */
    VK_REGS,
    /* an indexable temp. */
    VK_X,
    /* a uniform in a constant buffer. */
    VK_CB,
    VK_TGSM,
    /* objects: resources, samplers, streams and patches. */
    VK_OBJECT,
    /* a geometry shader's input array or a hull shader's input patch: inputs with a vertex index. */
    VK_VERTICES,
    /* a hull shader's output patch, read in the patch constant phase, and a domain shader's input patch. */
    VK_CONTROL_POINTS,
};

typedef struct ObjRef {
    /* the resource variable, or a stream or patch parameter. */
    HlVar *var;
    HlType *type;
    uint32_t element;
    /* a dynamic array index of shader model 5.1. */
    IrSrc dynamic;
    bool has_dynamic;
} ObjRef;

typedef struct VarInfo {
    uint8_t kind;
    HlVar *var;
    HlType *type;
    uint32_t count;
    /* VK_REGS. */
    uint32_t *home;
    IrSrc *known;
    uint32_t *known_epoch;
    /* VK_X: per component the register and component; unpacked puts each in a register of its own. */
    uint32_t x;
    uint32_t *xreg;
    uint8_t *xcomp;
    bool unpacked;
    /* VK_CB: the buffer and the byte offset of each component. */
    uint32_t buffer;
    uint32_t *offsets;
    /* VK_TGSM: the memory, and the byte offset of each component in an element. */
    uint32_t tgsm;
    uint32_t stride;
    /* VK_OBJECT: what the variable names, for parameters and locals; arrays of objects have one per element. */
    ObjRef *objects;
    uint32_t object_count;
    /* VK_VERTICES and VK_CONTROL_POINTS: the input register and component of each component of an element. */
    uint32_t *in_reg;
    uint8_t *in_comp;
    uint32_t vertex_components;
    /* the bool components, normalized on load. */
} VarInfo;

/* a value: its scalars, flattened. */
typedef struct Value {
    HlType *type;
    uint32_t count;
    IrSrc *c;
} Value;

enum {
    LV_NONE,
    LV_REGS,
    LV_X,
    LV_TGSM,
    /* an element of a typed view: coords in coord. */
    LV_TYPED,
    /* a part of an element of a structured view. */
    LV_STRUCTURED,
    /* a write-only output of the entry, or a hull shader's output control point. */
    LV_OUTPUT,
    /* components of a vector with a dynamic index, through movc. */
    LV_REGS_DYNAMIC,
};

typedef struct LValue {
    uint8_t kind;
    VarInfo *vi;
    HlType *type;
    uint32_t count;
    /* flat components of the variable, or byte offsets of structured elements. */
    uint32_t *comps;
    /* LV_X: registers added, as rel; LV_TGSM: the element; LV_REGS_DYNAMIC: the component index. */
    IrSrc dynamic;
    bool has_dynamic;
    /* LV_TGSM and LV_STRUCTURED: bytes added to the offsets. */
    IrSrc byte_offset;
    bool has_byte_offset;
    /* LV_REGS_DYNAMIC: the elements the index picks from. */
    uint32_t elements;
    /* LV_TYPED and LV_STRUCTURED. */
    ObjRef obj;
    Value coord;
} LValue;

/* a function being inlined. */
typedef struct Frame {
    HlFunc *func;
    struct Frame *outer;
    /* the return value's homes. */
    VarInfo *ret;
    /* set when a return ran, for code after returns that are not last. */
    struct VarInfo *flag;
    bool needs_flag;
    bool entry;
    /* the loops within the function, for returns from inside them. */
    uint32_t loops;
} Frame;

typedef struct UndoEntry {
    VarInfo *vi;
    uint32_t comp;
    IrSrc old;
} UndoEntry;

typedef struct Lower {
    IrShader *s;
    Hlsl *h;
    HlProgram *prog;
    HlFunc *entry;
    Frame *frame;
    UndoEntry *undo;
    uint32_t undo_count, undo_capacity;
    /* every VarInfo with known values, to forget them at loops. */
    VarInfo **live;
    uint32_t live_count, live_capacity;
    /* the epoch of known values: bumped at loop heads, which forget everything. */
    uint32_t epoch;
    uint32_t loops;
    /* compound assignments read their target once: the expression and its value. */
    HlExpr *subst;
    Value subst_value;
    /* the entry's outputs: the variable of each, and the element of its components. */
    VarInfo **outputs;
    uint32_t output_count, output_capacity;
    /* geometry shader streams: where the components of a vertex go. */
    struct Slot *stream_slots;
    /* hull shaders: the patch constant function and the control point id. */
    HlFunc *patch_function;
    bool in_fork;
    IrSrc control_point_id;
    /* the loop continue statements belong to. */
    HlStmt *loop_stmt;
    uint8_t *homes;
    uint32_t home_capacity;
    bool failed;
} Lower;

/* ------------------------------------------------------------------ building */

IrSrc lw_op1(Lower *l, uint16_t op, IrSrc a);
IrSrc lw_op2(Lower *l, uint16_t op, IrSrc a, IrSrc b);
IrSrc lw_op3(Lower *l, uint16_t op, IrSrc a, IrSrc b, IrSrc c);
IrSrc lw_neg(Lower *l, IrSrc a, uint8_t storage);
IrSrc lw_abs(Lower *l, IrSrc a, uint8_t storage);
/* the value as a canonical bool, 0 or ~0. */
IrSrc lw_to_bool(Lower *l, IrSrc a, uint8_t storage);
IrSrc lw_convert_scalar(Lower *l, IrSrc a, uint8_t from, uint8_t to);
IrSrc lw_movc(Lower *l, IrSrc c, IrSrc a, IrSrc b);
IrSrc lw_mov(Lower *l, IrSrc a);
Value lw_value(Lower *l, HlType *type, uint32_t count);
Value lw_expr(Lower *l, HlExpr *e);
bool lw_obj(Lower *l, HlExpr *e, ObjRef *out);
uint32_t lw_resource(Lower *l, const ObjRef *o);
IrSrc lw_resource_index(Lower *l, const ObjRef *o);
bool lw_lvalue(Lower *l, HlExpr *e, LValue *out);
Value lw_load(Lower *l, LValue *lv);
void lw_store(Lower *l, LValue *lv, Value v);
Value lw_convert(Lower *l, Value v, HlType *to);
IrSrc lw_scalar(Lower *l, HlExpr *e);
void lw_error(Lower *l, HlLoc loc, uint32_t code, const char *format, ...);
/* shader model 5.1 and later. */
static inline bool lw_sm51(const Lower *l) { return l->s->profile.major > 5 || (l->s->profile.major == 5 && l->s->profile.minor >= 1); }
static inline bool lw_sm5(const Lower *l) { return l->s->profile.major >= 5; }

/* ------------------------------------------------------------------ builtins */

Value lw_intrinsic(Lower *l, HlExpr *e);
Value lw_method(Lower *l, HlExpr *e);
/* reads or writes an element of a view through an index expression. */
Value lw_object_load(Lower *l, LValue *lv);
void lw_object_store(Lower *l, LValue *lv, Value v);

/* ------------------------------------------------------------------ the entry */

int lw_entry(Lower *l);
VarInfo *lw_new_local(Lower *l, HlVar *var, HlType *type);
/* runs the entry's body with its parameters in place; the return value. */
Value lw_entry_body(Lower *l, HlFunc *f);

#endif
