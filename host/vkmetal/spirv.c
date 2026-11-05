/*
 * geometry shaders on Metal, which has none, as vertex functions run with rasterization off. a vertex shader's
 * outputs go to a buffer of records, one a vertex, by the vertex's index. the geometry shader runs once a primitive
 * and instance, its inputs read from those records, and each vertex it emits is a record of its own with an index
 * in a strip cut by restarts. the same geometry shader again, as the vertex shader of the draw, reads the records
 * back into its outputs for the fragment shader.
 *
 * the rewrites work on the SPIR-V the guest's translators make: inputs and outputs are plain variables of 32-bit
 * scalars, vectors, matrices and arrays, built-ins among them, without blocks. a record has four words a location
 * and VKM_GS_BUILTINS words of built-ins after the last location.
 */
#include "vkm.h"

#include "spirv.h"
#include "vkm_msl.h"

enum {
    BI_POSITION = 0,
    BI_POINT_SIZE = 4,
    BI_CLIP = 5,
    BI_CULL = 13,
    BI_LAYER = 21,
    BI_VIEWPORT = 22,
    BI_PRIMITIVE = 23,
};

typedef struct Buf {
    uint32_t *w;
    size_t n, cap;
    bool oom;
} Buf;

typedef struct Var {
    uint32_t id, pointee, storage;
    uint32_t builtin; /* ~0 for none. */
    int32_t location; /* -1 for none. */
    uint32_t component;
    bool convert;  /* becomes a private variable. */
    bool relocate; /* a built-in output a vertex shader cannot have, as a location. */
} Var;

enum { K_FLOAT, K_INT, K_UINT };

typedef struct Rw {
    const uint32_t *in;
    size_t n;
    uint32_t bound;
    uint32_t *at; /* each id's defining instruction, as a word offset. */
    uint32_t *builtin, *location, *component;
    Var *vars;
    uint32_t var_count;
    uint32_t entry, model, entry_offset;
    uint32_t version;
    uint32_t in_vertices, out_kind, max_vertices, invocations;
    Buf annot, types, funcs;
    uint32_t t_void, t_bool, t_scalar[3], t_fn;
    uint32_t sb_uint; /* pointer to a storage buffer word. */
    struct {
        uint32_t storage, type, ptr;
    } ptrs[32];
    uint32_t ptr_count;
    struct {
        uint32_t value, id;
    } consts[64];
    uint32_t const_count;
    uint32_t new_inputs[4];
    uint32_t new_input_count;
    uint32_t buffers[2];
    uint32_t buffer_count;
    bool failed;
} Rw;

static void put(Buf *b, uint32_t v)
{
    if (b->n == b->cap) {
        size_t cap = b->cap ? b->cap * 2 : 256;
        uint32_t *w = realloc(b->w, cap * sizeof(*w));
        if (!w) {
            b->oom = true;
            return;
        }
        b->w = w;
        b->cap = cap;
    }
    b->w[b->n++] = v;
}

static void inst(Buf *b, uint32_t op, const uint32_t *ops, uint32_t count)
{
    put(b, (count + 1) << 16 | op);
    for (uint32_t i = 0; i < count; i++)
        put(b, ops[i]);
}

static uint32_t new_id(Rw *r)
{
    return r->bound++;
}

static uint32_t op_of(const uint32_t *w)
{
    return w[0] & 0xffff;
}

static uint32_t len_of(const uint32_t *w)
{
    return w[0] >> 16;
}

static const uint32_t *def(const Rw *r, uint32_t id)
{
    return id < r->bound && r->at[id] ? r->in + r->at[id] : nullptr;
}

/* ------------------------------------------------------------------ types and constants. */

/* an existing declaration of a type that may not be declared twice. */
static uint32_t find_type(const Rw *r, uint32_t op, const uint32_t *operands, uint32_t count)
{
    for (size_t at = 5; at < r->n;) {
        const uint32_t *w = r->in + at;
        uint32_t len = len_of(w);
        if (!len)
            break;
        if (op_of(w) == op && len == count + 2 && !memcmp(w + 2, operands, count * 4))
            return w[1];
        if (op_of(w) == SpvOpFunction)
            break;
        at += len;
    }
    return 0;
}

static uint32_t type(Rw *r, uint32_t op, const uint32_t *operands, uint32_t count)
{
    uint32_t id = find_type(r, op, operands, count);
    if (id)
        return id;
    id = new_id(r);
    put(&r->types, (count + 2) << 16 | op);
    put(&r->types, id);
    for (uint32_t i = 0; i < count; i++)
        put(&r->types, operands[i]);
    return id;
}

static void types_init(Rw *r)
{
    r->t_void = type(r, SpvOpTypeVoid, nullptr, 0);
    r->t_bool = type(r, SpvOpTypeBool, nullptr, 0);
    r->t_scalar[K_FLOAT] = type(r, SpvOpTypeFloat, (uint32_t[]){32}, 1);
    r->t_scalar[K_INT] = type(r, SpvOpTypeInt, (uint32_t[]){32, 1}, 2);
    r->t_scalar[K_UINT] = type(r, SpvOpTypeInt, (uint32_t[]){32, 0}, 2);
    r->t_fn = type(r, SpvOpTypeFunction, &r->t_void, 1);
}

static uint32_t ptr(Rw *r, uint32_t storage, uint32_t pointee)
{
    for (uint32_t i = 0; i < r->ptr_count; i++)
        if (r->ptrs[i].storage == storage && r->ptrs[i].type == pointee)
            return r->ptrs[i].ptr;
    uint32_t id = new_id(r);
    inst(&r->types, SpvOpTypePointer, (uint32_t[]){id, storage, pointee}, 3);
    if (r->ptr_count < MIME_ARRAY_COUNT(r->ptrs))
        r->ptrs[r->ptr_count++] = (typeof(r->ptrs[0])){storage, pointee, id};
    return id;
}

static uint32_t uconst(Rw *r, uint32_t v)
{
    for (uint32_t i = 0; i < r->const_count; i++)
        if (r->consts[i].value == v)
            return r->consts[i].id;
    uint32_t id = new_id(r);
    inst(&r->types, SpvOpConstant, (uint32_t[]){r->t_scalar[K_UINT], id, v}, 3);
    if (r->const_count < MIME_ARRAY_COUNT(r->consts))
        r->consts[r->const_count++] = (typeof(r->consts[0])){v, id};
    return id;
}

static uint32_t constant_value(const Rw *r, uint32_t id)
{
    const uint32_t *w = def(r, id);
    return w && op_of(w) == SpvOpConstant ? w[3] : 0;
}

/* the kind of a 32-bit scalar type, or -1. */
static int scalar_kind(const Rw *r, uint32_t id)
{
    const uint32_t *w = def(r, id);
    if (!w)
        return -1;
    if (op_of(w) == SpvOpTypeFloat && w[2] == 32)
        return K_FLOAT;
    if (op_of(w) == SpvOpTypeInt && w[2] == 32)
        return w[3] ? K_INT : K_UINT;
    return -1;
}

/* locations a type takes, and its 32-bit scalars. */
static uint32_t locations(const Rw *r, uint32_t id, bool dense)
{
    const uint32_t *w = def(r, id);
    if (!w)
        return 0;
    switch (op_of(w)) {
    case SpvOpTypeFloat:
    case SpvOpTypeInt: return 1;
    case SpvOpTypeVector: return dense ? w[3] : 1;
    case SpvOpTypeMatrix: return w[3] * locations(r, w[2], dense);
    case SpvOpTypeArray: return constant_value(r, w[3]) * locations(r, w[2], dense);
    case SpvOpTypeStruct: {
        uint32_t n = 0;
        for (uint32_t i = 2; i < len_of(w); i++)
            n += locations(r, w[i], dense);
        return n;
    }
    default: return 0;
    }
}

/* ------------------------------------------------------------------ code. */

static uint32_t emit(Rw *r, uint32_t op, uint32_t result_type, const uint32_t *args, uint32_t count)
{
    uint32_t id = new_id(r);
    put(&r->funcs, (count + 3) << 16 | op);
    put(&r->funcs, result_type);
    put(&r->funcs, id);
    for (uint32_t i = 0; i < count; i++)
        put(&r->funcs, args[i]);
    return id;
}

#define E(r, op, t, ...) emit(r, op, t, (uint32_t[]){__VA_ARGS__}, sizeof((uint32_t[]){__VA_ARGS__}) / 4)
#define U(r) ((r)->t_scalar[K_UINT])

static uint32_t label(Rw *r)
{
    uint32_t id = new_id(r);
    inst(&r->funcs, SpvOpLabel, &id, 1);
    return id;
}

static void label_id(Rw *r, uint32_t id)
{
    inst(&r->funcs, SpvOpLabel, &id, 1);
}

static void branch(Rw *r, uint32_t to)
{
    inst(&r->funcs, SpvOpBranch, &to, 1);
}

static uint32_t function_begin(Rw *r)
{
    uint32_t id = new_id(r);
    inst(&r->funcs, SpvOpFunction, (uint32_t[]){r->t_void, id, 0, r->t_fn}, 4);
    label(r);
    return id;
}

static void function_end(Rw *r)
{
    inst(&r->funcs, SpvOpReturn, nullptr, 0);
    inst(&r->funcs, SpvOpFunctionEnd, nullptr, 0);
}

/* an if without an else; the code after it goes on in the returned block. */
static uint32_t if_begin(Rw *r, uint32_t condition)
{
    uint32_t then = new_id(r), end = new_id(r);
    inst(&r->funcs, SpvOpSelectionMerge, (uint32_t[]){end, 0}, 2);
    inst(&r->funcs, SpvOpBranchConditional, (uint32_t[]){condition, then, end}, 3);
    label_id(r, then);
    return end;
}

static void if_end(Rw *r, uint32_t end)
{
    branch(r, end);
    label_id(r, end);
}

static uint32_t load(Rw *r, uint32_t type, uint32_t pointer)
{
    return E(r, SpvOpLoad, type, pointer);
}

static void store(Rw *r, uint32_t pointer, uint32_t value)
{
    inst(&r->funcs, SpvOpStore, (uint32_t[]){pointer, value}, 2);
}

static uint32_t add(Rw *r, uint32_t a, uint32_t b)
{
    return E(r, SpvOpIAdd, U(r), a, b);
}

static uint32_t mul(Rw *r, uint32_t a, uint32_t b)
{
    return E(r, SpvOpIMul, U(r), a, b);
}

static uint32_t word_ptr(Rw *r, uint32_t buffer, uint32_t index)
{
    return E(r, SpvOpAccessChain, r->sb_uint, buffer, uconst(r, 0), index);
}

static uint32_t read_word(Rw *r, uint32_t buffer, uint32_t index)
{
    return load(r, U(r), word_ptr(r, buffer, index));
}

static void write_word(Rw *r, uint32_t buffer, uint32_t index, uint32_t value)
{
    store(r, word_ptr(r, buffer, index), value);
}

static uint32_t header(Rw *r, uint32_t word)
{
    return read_word(r, r->buffers[0], uconst(r, word));
}

static uint32_t private_var(Rw *r, uint32_t pointee)
{
    uint32_t id = new_id(r);
    inst(&r->types, SpvOpVariable, (uint32_t[]){ptr(r, SpvStorageClassPrivate, pointee), id, SpvStorageClassPrivate},
         3);
    return id;
}

/* a loop over the private counter from its value to the limit. the body goes on in the block it returns into. */
typedef struct Loop {
    uint32_t header, cont, merge, counter;
} Loop;

static uint32_t loop_begin(Rw *r, Loop *l, uint32_t counter, uint32_t limit)
{
    l->header = new_id(r);
    l->cont = new_id(r);
    l->merge = new_id(r);
    l->counter = counter;
    uint32_t check = new_id(r), body = new_id(r);
    branch(r, l->header);
    label_id(r, l->header);
    inst(&r->funcs, SpvOpLoopMerge, (uint32_t[]){l->merge, l->cont, 0}, 3);
    branch(r, check);
    label_id(r, check);
    uint32_t value = load(r, U(r), counter);
    uint32_t less = E(r, SpvOpULessThan, r->t_bool, value, limit);
    inst(&r->funcs, SpvOpBranchConditional, (uint32_t[]){less, body, l->merge}, 3);
    label_id(r, body);
    return value;
}

static void loop_end(Rw *r, Loop *l)
{
    branch(r, l->cont);
    label_id(r, l->cont);
    store(r, l->counter, add(r, load(r, U(r), l->counter), uconst(r, 1)));
    branch(r, l->header);
    label_id(r, l->merge);
}

/* ------------------------------------------------------------------ records. */

static int32_t builtin_word(uint32_t builtin)
{
    switch (builtin) {
    case SpvBuiltInPosition: return BI_POSITION;
    case SpvBuiltInPointSize: return BI_POINT_SIZE;
    case SpvBuiltInClipDistance: return BI_CLIP;
    case SpvBuiltInCullDistance: return BI_CULL;
    case SpvBuiltInLayer: return BI_LAYER;
    case SpvBuiltInViewportIndex: return BI_VIEWPORT;
    case SpvBuiltInPrimitiveId: return BI_PRIMITIVE;
    default: return -1;
    }
}

/* how a variable's scalars move between it and a record. */
typedef struct Move {
    uint32_t var, storage;
    uint32_t buffer, base;  /* the record's first word. */
    uint32_t builtins;      /* the word of its built-ins, from its first. */
    bool to_record;
    uint32_t path[8];
    uint32_t depth;
} Move;

static void move_scalar(Rw *r, Move *m, int kind, uint32_t word, bool builtin)
{
    uint32_t scalar = r->t_scalar[kind];
    uint32_t pointer = m->var;
    if (m->depth) {
        uint32_t args[10] = {m->var};
        memcpy(args + 1, m->path, m->depth * 4);
        pointer = emit(r, SpvOpAccessChain, ptr(r, m->storage, scalar), args, m->depth + 1);
    }
    uint32_t at = add(r, m->base, builtin ? add(r, m->builtins, uconst(r, word)) : uconst(r, word));
    if (m->to_record) {
        uint32_t v = load(r, scalar, pointer);
        write_word(r, m->buffer, at, kind == K_UINT ? v : E(r, SpvOpBitcast, U(r), v));
    } else {
        uint32_t v = read_word(r, m->buffer, at);
        store(r, pointer, kind == K_UINT ? v : E(r, SpvOpBitcast, scalar, v));
    }
}

/* each scalar of the type at the path; located ones take a location an element, built-ins are dense. */
static void move_type(Rw *r, Move *m, uint32_t id, uint32_t word, bool builtin)
{
    const uint32_t *w = def(r, id);
    int kind = scalar_kind(r, id);
    if (kind >= 0) {
        move_scalar(r, m, kind, word, builtin);
        return;
    }
    if (!w || m->depth >= MIME_ARRAY_COUNT(m->path)) {
        r->failed = true;
        return;
    }
    switch (op_of(w)) {
    case SpvOpTypeVector:
    case SpvOpTypeMatrix:
    case SpvOpTypeArray: {
        uint32_t count = op_of(w) == SpvOpTypeArray ? constant_value(r, w[3]) : w[3];
        uint32_t step = op_of(w) == SpvOpTypeVector ? 1 : builtin ? locations(r, w[2], true) : locations(r, w[2], false) * 4;
        for (uint32_t i = 0; i < count; i++) {
            m->path[m->depth++] = uconst(r, i);
            move_type(r, m, w[2], word + i * step, builtin);
            m->depth--;
        }
        break;
    }
    case SpvOpTypeStruct:
        for (uint32_t i = 2; i < len_of(w); i++) {
            m->path[m->depth++] = uconst(r, i - 2);
            move_type(r, m, w[i], word, builtin);
            m->depth--;
            word += builtin ? locations(r, w[i], true) : locations(r, w[i], false) * 4;
        }
        break;
    default:
        r->failed = true;
        break;
    }
}

/* a variable's element at the path prefix to or from the record at base. */
static void move_var(Rw *r, const Var *v, uint32_t storage, const uint32_t *prefix, uint32_t prefix_count,
                     uint32_t element, uint32_t buffer, uint32_t base, uint32_t builtins, bool to_record)
{
    Move m = {v->id, storage, buffer, base, builtins, to_record};
    memcpy(m.path, prefix, prefix_count * 4);
    m.depth = prefix_count;
    if (v->builtin != ~0u) {
        int32_t word = builtin_word(v->builtin);
        if (word >= 0)
            move_type(r, &m, element, (uint32_t)word, true);
    } else if (v->location >= 0) {
        move_type(r, &m, element, (uint32_t)v->location * 4 + v->component, false);
    }
}

/* the locations a record has for these variables, the outer array of per vertex ones aside. */
static uint32_t record_locations(const Rw *r, uint32_t storage, bool arrayed)
{
    uint32_t n = 0;
    for (uint32_t i = 0; i < r->var_count; i++) {
        const Var *v = &r->vars[i];
        if (v->storage != storage || v->location < 0)
            continue;
        uint32_t element = v->pointee;
        const uint32_t *w = def(r, element);
        if (arrayed && w && op_of(w) == SpvOpTypeArray)
            element = w[2];
        n = MIME_MAX(n, (uint32_t)v->location + locations(r, element, false));
    }
    return n;
}

static uint32_t storage_buffer(Rw *r, uint32_t binding)
{
    uint32_t rta = new_id(r), block = new_id(r), var = new_id(r);
    inst(&r->types, SpvOpTypeRuntimeArray, (uint32_t[]){rta, U(r)}, 2);
    inst(&r->types, SpvOpTypeStruct, (uint32_t[]){block, rta}, 2);
    uint32_t p = new_id(r);
    inst(&r->types, SpvOpTypePointer, (uint32_t[]){p, SpvStorageClassStorageBuffer, block}, 3);
    inst(&r->types, SpvOpVariable, (uint32_t[]){p, var, SpvStorageClassStorageBuffer}, 3);
    inst(&r->annot, SpvOpDecorate, (uint32_t[]){rta, SpvDecorationArrayStride, 4}, 3);
    inst(&r->annot, SpvOpDecorate, (uint32_t[]){block, SpvDecorationBlock}, 2);
    inst(&r->annot, SpvOpMemberDecorate, (uint32_t[]){block, 0, SpvDecorationOffset, 0}, 4);
    inst(&r->annot, SpvOpDecorate, (uint32_t[]){var, SpvDecorationDescriptorSet, VKM_EMU_SET}, 3);
    inst(&r->annot, SpvOpDecorate, (uint32_t[]){var, SpvDecorationBinding, binding}, 3);
    r->buffers[r->buffer_count++] = var;
    return var;
}

/* a built-in input, as a uint; one of the shader's own when it has it. */
static uint32_t builtin_input(Rw *r, uint32_t builtin)
{
    for (uint32_t i = 0; i < r->var_count; i++) {
        const Var *v = &r->vars[i];
        if (v->storage == SpvStorageClassInput && v->builtin == builtin && !v->convert) {
            uint32_t value = load(r, v->pointee, v->id);
            return scalar_kind(r, v->pointee) == K_UINT ? value : E(r, SpvOpBitcast, U(r), value);
        }
    }
    for (uint32_t i = 0; i < r->new_input_count; i++)
        if (r->new_inputs[i] >> 16 == builtin)
            return load(r, U(r), r->new_inputs[i] & 0xffff);
    uint32_t var = new_id(r);
    inst(&r->types, SpvOpVariable, (uint32_t[]){ptr(r, SpvStorageClassInput, U(r)), var, SpvStorageClassInput}, 3);
    inst(&r->annot, SpvOpDecorate, (uint32_t[]){var, SpvDecorationBuiltIn, builtin}, 3);
    if (var < 0x10000 && r->new_input_count < MIME_ARRAY_COUNT(r->new_inputs))
        r->new_inputs[r->new_input_count++] = builtin << 16 | var;
    else
        r->failed = true;
    return load(r, U(r), var);
}

/* ------------------------------------------------------------------ parsing. */

static bool parse(Rw *r, const uint32_t *in, size_t words)
{
    if (words < 5 || in[0] != SpvMagicNumber)
        return false;
    r->in = in;
    r->n = words;
    r->version = in[1];
    r->bound = in[3];
    uint32_t bound = in[3];
    r->at = calloc(bound, 4);
    r->builtin = malloc(bound * 4);
    r->location = malloc(bound * 4);
    r->component = calloc(bound, 4);
    r->vars = calloc(bound, sizeof(*r->vars));
    if (!r->at || !r->builtin || !r->location || !r->component || !r->vars)
        return false;
    memset(r->builtin, 0xff, bound * 4);
    memset(r->location, 0xff, bound * 4);
    bool in_function = false;
    for (size_t at = 5; at < words;) {
        const uint32_t *w = in + at;
        uint32_t op = op_of(w), len = len_of(w);
        if (!len || at + len > words)
            return false;
        switch (op) {
        case SpvOpEntryPoint:
            if (!r->entry_offset) {
                r->model = w[1];
                r->entry = w[2];
                r->entry_offset = (uint32_t)at;
            }
            break;
        case SpvOpExecutionMode:
            if (w[1] == r->entry) {
                switch (w[2]) {
                case SpvExecutionModeInputPoints: r->in_vertices = 1; break;
                case SpvExecutionModeInputLines: r->in_vertices = 2; break;
                case SpvExecutionModeInputLinesAdjacency: r->in_vertices = 4; break;
                case SpvExecutionModeTriangles: r->in_vertices = 3; break;
                case SpvExecutionModeInputTrianglesAdjacency: r->in_vertices = 6; break;
                case SpvExecutionModeOutputPoints: r->out_kind = 0; break;
                case SpvExecutionModeOutputLineStrip: r->out_kind = 1; break;
                case SpvExecutionModeOutputTriangleStrip: r->out_kind = 2; break;
                case SpvExecutionModeOutputVertices: r->max_vertices = w[3]; break;
                case SpvExecutionModeInvocations: r->invocations = w[3]; break;
                }
            }
            break;
        case SpvOpDecorate:
            if (w[1] < bound) {
                if (w[2] == SpvDecorationBuiltIn)
                    r->builtin[w[1]] = w[3];
                else if (w[2] == SpvDecorationLocation)
                    r->location[w[1]] = w[3];
                else if (w[2] == SpvDecorationComponent)
                    r->component[w[1]] = w[3];
            }
            break;
        case SpvOpFunction: in_function = true; break;
        case SpvOpFunctionEnd: in_function = false; break;
        case SpvOpVariable:
            if (!in_function && w[2] < bound) {
                r->at[w[2]] = (uint32_t)at;
                const uint32_t *p = def(r, w[1]);
                if (w[3] == SpvStorageClassInput || w[3] == SpvStorageClassOutput)
                    r->vars[r->var_count++] = (Var){w[2], p ? p[3] : 0, w[3]};
            }
            break;
        default:
            if (!in_function && op >= SpvOpTypeVoid && op <= SpvOpTypeForwardPointer && len >= 2 && w[1] < bound)
                r->at[w[1]] = (uint32_t)at;
            else if (!in_function && (op == SpvOpConstant || op == SpvOpSpecConstant) && len >= 3 && w[2] < bound)
                r->at[w[2]] = (uint32_t)at;
            break;
        }
        at += len;
    }
    for (uint32_t i = 0; i < r->var_count; i++) {
        Var *v = &r->vars[i];
        v->builtin = r->builtin[v->id];
        v->location = r->location[v->id] == ~0u ? -1 : (int32_t)r->location[v->id];
        v->component = r->component[v->id];
        const uint32_t *t = def(r, v->pointee);
        /* blocks of built-ins are not what the guest makes. */
        if (!t || op_of(t) == SpvOpTypeStruct)
            return false;
    }
    return r->entry != 0;
}

static void rw_free(Rw *r)
{
    free(r->at);
    free(r->builtin);
    free(r->location);
    free(r->component);
    free(r->vars);
    free(r->annot.w);
    free(r->types.w);
    free(r->funcs.w);
}

static const Var *var_of(const Rw *r, uint32_t id)
{
    for (uint32_t i = 0; i < r->var_count; i++)
        if (r->vars[i].id == id)
            return &r->vars[i];
    return nullptr;
}

/* ------------------------------------------------------------------ writing the module back. */

typedef struct Calls {
    uint32_t prologue, epilogue, emit, cut;
    uint32_t new_entry; /* replaces the entry point's function. */
    uint32_t model;
    bool inputs, outputs; /* converted. */
    bool drop_modes, drop_primitives;
} Calls;

static bool annotation(uint32_t op)
{
    return op == SpvOpDecorate || op == SpvOpMemberDecorate || op == SpvOpDecorationGroup ||
           op == SpvOpGroupDecorate || op == SpvOpGroupMemberDecorate || op == SpvOpDecorateId ||
           op == SpvOpDecorateString || op == SpvOpMemberDecorateString;
}

static bool preamble(uint32_t op)
{
    return op == SpvOpCapability || op == SpvOpExtension || op == SpvOpExtInstImport || op == SpvOpMemoryModel ||
           op == SpvOpEntryPoint || op == SpvOpExecutionMode || op == SpvOpExecutionModeId || op == SpvOpString ||
           op == SpvOpSourceExtension || op == SpvOpSource || op == SpvOpSourceContinued || op == SpvOpName ||
           op == SpvOpMemberName || op == SpvOpModuleProcessed || annotation(op);
}

static void call(Rw *r, Buf *out, uint32_t fn)
{
    inst(out, SpvOpFunctionCall, (uint32_t[]){r->t_void, new_id(r), fn}, 3);
}

static bool converted(const Rw *r, uint32_t id)
{
    const Var *v = var_of(r, id);
    return v && v->convert;
}

static bool write_module(Rw *r, const Calls *c, uint32_t **out, size_t *out_words)
{
    Buf o = {};
    for (int i = 0; i < 5; i++)
        put(&o, r->in[i]);
    bool annot_done = false, types_done = false, extension_done = r->version >= 0x10300;
    bool in_entry = false, prologue_pending = false;
    for (size_t at = 5; at < r->n;) {
        const uint32_t *w = r->in + at;
        uint32_t op = op_of(w), len = len_of(w);
        at += len;
        if (!extension_done && op != SpvOpCapability) {
            static const char name[] = "SPV_KHR_storage_buffer_storage_class";
            uint32_t words[(sizeof(name) + 3) / 4] = {};
            memcpy(words, name, sizeof(name));
            inst(&o, SpvOpExtension, words, MIME_ARRAY_COUNT(words));
            extension_done = true;
        }
        if (!annot_done && !preamble(op)) {
            for (size_t i = 0; i < r->annot.n; i++)
                put(&o, r->annot.w[i]);
            annot_done = true;
        }
        if (!types_done && op == SpvOpFunction) {
            for (size_t i = 0; i < r->types.n; i++)
                put(&o, r->types.w[i]);
            types_done = true;
        }
        switch (op) {
        case SpvOpEntryPoint: {
            if (at - len != r->entry_offset)
                break;
            /* the name runs to its terminating zero, the interface after it. */
            uint32_t name_end = 3;
            while (name_end < len && (w[name_end] >> 24))
                name_end++;
            name_end++;
            uint32_t start = (uint32_t)o.n;
            put(&o, 0);
            put(&o, c->model);
            put(&o, c->new_entry ? c->new_entry : w[2]);
            for (uint32_t i = 3; i < name_end; i++)
                put(&o, w[i]);
            for (uint32_t i = name_end; i < len; i++)
                if (!converted(r, w[i]))
                    put(&o, w[i]);
            for (uint32_t i = 0; i < r->new_input_count; i++)
                put(&o, r->new_inputs[i] & 0xffff);
            if (r->version >= 0x10400)
                for (uint32_t i = 0; i < r->buffer_count; i++)
                    put(&o, r->buffers[i]);
            o.w[start] = (uint32_t)(o.n - start) << 16 | SpvOpEntryPoint;
            continue;
        }
        case SpvOpExecutionMode:
        case SpvOpExecutionModeId:
            if (c->drop_modes && w[1] == r->entry)
                continue;
            break;
        case SpvOpDecorate:
        case SpvOpDecorateId:
        case SpvOpDecorateString: {
            if (converted(r, w[1]))
                continue;
            const Var *v = var_of(r, w[1]);
            if (op == SpvOpDecorate && v && v->relocate && w[2] == SpvDecorationBuiltIn) {
                inst(&o, SpvOpDecorate, (uint32_t[]){w[1], SpvDecorationLocation, 31}, 3);
                continue;
            }
            break;
        }
        case SpvOpTypePointer:
            /* a storage class is converted whole, so every pointer of it is private. */
            if ((w[2] == SpvStorageClassInput && c->inputs) || (w[2] == SpvStorageClassOutput && c->outputs)) {
                inst(&o, SpvOpTypePointer, (uint32_t[]){w[1], SpvStorageClassPrivate, w[3]}, 3);
                continue;
            }
            break;
        case SpvOpVariable:
            if (converted(r, w[2])) {
                inst(&o, SpvOpVariable, (uint32_t[]){w[1], w[2], SpvStorageClassPrivate, len > 4 ? w[4] : 0},
                     len > 4 ? 4 : 3);
                continue;
            }
            break;
        case SpvOpFunction:
            in_entry = w[2] == r->entry;
            break;
        case SpvOpFunctionEnd:
            in_entry = false;
            break;
        case SpvOpLabel:
            for (uint32_t i = 0; i < len; i++)
                put(&o, w[i]);
            if (in_entry && c->prologue && !prologue_pending) {
                prologue_pending = true;
                /* after the block's variables. */
                while (at < r->n && (op_of(r->in + at) == SpvOpVariable || op_of(r->in + at) == SpvOpLine)) {
                    for (uint32_t i = 0; i < len_of(r->in + at); i++)
                        put(&o, r->in[at + i]);
                    at += len_of(r->in + at);
                }
                call(r, &o, c->prologue);
            }
            continue;
        case SpvOpReturn:
            if (in_entry && c->epilogue)
                call(r, &o, c->epilogue);
            break;
        case SpvOpEmitVertex:
        case SpvOpEmitStreamVertex:
        case SpvOpEndPrimitive:
        case SpvOpEndStreamPrimitive: {
            bool emit_op = op == SpvOpEmitVertex || op == SpvOpEmitStreamVertex;
            bool raster = len < 2 || !constant_value(r, w[1]);
            if (!c->drop_primitives && raster)
                call(r, &o, emit_op ? c->emit : c->cut);
            continue;
        }
        }
        for (uint32_t i = 0; i < len; i++)
            put(&o, w[i]);
    }
    for (size_t i = 0; i < r->funcs.n; i++)
        put(&o, r->funcs.w[i]);
    o.w[3] = r->bound;
    if (o.oom || r->annot.oom || r->types.oom || r->funcs.oom || r->failed) {
        free(o.w);
        return false;
    }
    *out = o.w;
    *out_words = o.n;
    return true;
}

/* ------------------------------------------------------------------ the three rewrites. */

/*
 * a vertex shader whose outputs go to the record of its vertex: slot vertex index - VKM_GS_VBASE of instance
 * instance index - VKM_GS_BASE_INSTANCE, when the slot is under VKM_GS_CAP.
 */
static bool capture(Rw *r, uint32_t **out, size_t *out_words, uint32_t *record_words, uint32_t *builtins)
{
    for (uint32_t i = 0; i < r->var_count; i++)
        r->vars[i].convert = r->vars[i].storage == SpvStorageClassOutput;
    uint32_t locs = record_locations(r, SpvStorageClassOutput, false);
    *builtins = locs * 4;
    *record_words = locs * 4 + VKM_GS_BUILTINS;
    storage_buffer(r, 0);
    r->sb_uint = ptr(r, SpvStorageClassStorageBuffer, U(r));
    Calls c = {.model = SpvExecutionModelVertex, .outputs = true};
    c.epilogue = function_begin(r);
    uint32_t emu = r->buffers[0];
    uint32_t slot = E(r, SpvOpISub, U(r), builtin_input(r, SpvBuiltInVertexIndex), header(r, VKM_GS_VBASE));
    uint32_t instance =
        E(r, SpvOpISub, U(r), builtin_input(r, SpvBuiltInInstanceIndex), header(r, VKM_GS_BASE_INSTANCE));
    uint32_t cap = header(r, VKM_GS_CAP);
    uint32_t end = if_begin(r, E(r, SpvOpULessThan, r->t_bool, slot, cap));
    uint32_t record = add(r, mul(r, instance, cap), slot);
    uint32_t base = add(r, header(r, VKM_GS_IN_BASE), mul(r, record, header(r, VKM_GS_IN_STRIDE)));
    uint32_t bi = uconst(r, *builtins);
    for (uint32_t i = 0; i < r->var_count; i++)
        if (r->vars[i].convert)
            move_var(r, &r->vars[i], SpvStorageClassPrivate, nullptr, 0, r->vars[i].pointee, emu, base, bi, true);
    if_end(r, end);
    function_end(r);
    return write_module(r, &c, out, out_words);
}

/* the uint vertex of the per vertex input array, where the geometry shader's k-th input vertex is. */
static uint32_t input_slot(Rw *r, uint32_t p, uint32_t k, uint32_t swap)
{
    uint32_t emu = r->buffers[0], idx = r->buffers[1];
    uint32_t pos = mul(r, header(r, VKM_GS_MUL), p);
    uint32_t straight = uconst(r, k);
    if (k < 2)
        straight = E(r, SpvOpSelect, U(r), swap, uconst(r, 1 - k), straight);
    pos = add(r, pos, straight);
    uint32_t type = header(r, VKM_GS_INDEX_TYPE);
    uint32_t element = add(r, header(r, VKM_GS_INDEX_FIRST), pos);
    uint32_t short_index = E(r, SpvOpIEqual, r->t_bool, type, uconst(r, 1));
    uint32_t none = E(r, SpvOpIEqual, r->t_bool, type, uconst(r, 0));
    uint32_t word_at = E(r, SpvOpSelect, U(r), short_index, E(r, SpvOpShiftRightLogical, U(r), element, uconst(r, 1)),
                         element);
    word_at = E(r, SpvOpSelect, U(r), none, uconst(r, 0), word_at);
    uint32_t word = read_word(r, idx, word_at);
    uint32_t high = E(r, SpvOpINotEqual, r->t_bool, E(r, SpvOpBitwiseAnd, U(r), element, uconst(r, 1)), uconst(r, 0));
    uint32_t half = E(r, SpvOpSelect, U(r), high, E(r, SpvOpShiftRightLogical, U(r), word, uconst(r, 16)),
                      E(r, SpvOpBitwiseAnd, U(r), word, uconst(r, 0xffff)));
    uint32_t slot = E(r, SpvOpSelect, U(r), short_index, half, word);
    slot = E(r, SpvOpSelect, U(r), none, pos, slot);
    (void)emu;
    return E(r, SpvOpSelect, U(r), E(r, SpvOpULessThan, r->t_bool, slot, header(r, VKM_GS_CAP)), slot, uconst(r, 0));
}

static bool geometry(Rw *r, bool replay, VkmGsInfo *info)
{
    if (r->model != SpvExecutionModelGeometry || !r->in_vertices)
        return false;
    uint32_t M = MIME_MAX(r->max_vertices, 1u), I = MIME_MAX(r->invocations, 1u), N = r->in_vertices;
    for (uint32_t i = 0; i < r->var_count; i++) {
        Var *v = &r->vars[i];
        v->convert = v->storage == SpvStorageClassInput || (v->storage == SpvStorageClassOutput && !replay);
        v->relocate = replay && v->storage == SpvStorageClassOutput && v->builtin == SpvBuiltInPrimitiveId;
    }
    uint32_t out_locs = record_locations(r, SpvStorageClassOutput, false);
    uint32_t out_stride = out_locs * 4 + VKM_GS_BUILTINS;
    bool strip = r->out_kind != 0;
    *info = (VkmGsInfo){N, r->out_kind, M, I, out_stride, out_locs * 4};
    storage_buffer(r, 0);
    if (!replay)
        storage_buffer(r, 1);
    r->sb_uint = ptr(r, SpvStorageClassStorageBuffer, U(r));
    uint32_t emu = r->buffers[0];
    uint32_t obi = uconst(r, out_locs * 4);
    Calls c = {.model = SpvExecutionModelVertex, .inputs = true, .outputs = !replay, .drop_modes = true,
               .drop_primitives = replay};

    if (replay) {
        c.new_entry = function_begin(r);
        uint32_t record = builtin_input(r, SpvBuiltInVertexIndex);
        uint32_t base = add(r, header(r, VKM_GS_OUT_BASE), mul(r, record, uconst(r, out_stride)));
        for (uint32_t i = 0; i < r->var_count; i++) {
            const Var *v = &r->vars[i];
            if (v->storage == SpvStorageClassOutput && !v->relocate)
                move_var(r, v, SpvStorageClassOutput, nullptr, 0, v->pointee, emu, base, obi, false);
        }
        function_end(r);
        return write_module(r, &c, &info->code, &info->words);
    }

    uint32_t g_inv = private_var(r, U(r)), g_n = private_var(r, U(r)), g_k = private_var(r, U(r));
    uint32_t idx_count = 2 * M;

    /* the inputs of the primitive, and which one and which invocation this is. */
    c.prologue = function_begin(r);
    uint32_t vertex = builtin_input(r, SpvBuiltInVertexIndex), instance = builtin_input(r, SpvBuiltInInstanceIndex);
    uint32_t prims = header(r, VKM_GS_PRIMS);
    uint32_t inv = E(r, SpvOpUMod, U(r), vertex, uconst(r, I)), p = E(r, SpvOpUDiv, U(r), vertex, uconst(r, I));
    store(r, g_inv, add(r, mul(r, instance, mul(r, prims, uconst(r, I))), vertex));
    store(r, g_n, uconst(r, 0));
    store(r, g_k, uconst(r, 0));
    uint32_t odd = E(r, SpvOpINotEqual, r->t_bool, E(r, SpvOpBitwiseAnd, U(r), p, uconst(r, 1)), uconst(r, 0));
    uint32_t swap = E(r, SpvOpLogicalAnd, r->t_bool, odd,
                      E(r, SpvOpINotEqual, r->t_bool, header(r, VKM_GS_SWAP), uconst(r, 0)));
    uint32_t cap = header(r, VKM_GS_CAP), stride = header(r, VKM_GS_IN_STRIDE), in_base = header(r, VKM_GS_IN_BASE);
    uint32_t ibi = header(r, VKM_GS_IN_BUILTINS);
    for (uint32_t k = 0; k < N; k++) {
        uint32_t slot = input_slot(r, p, k, swap);
        uint32_t base = add(r, in_base, mul(r, add(r, mul(r, instance, cap), slot), stride));
        uint32_t index = uconst(r, k);
        for (uint32_t i = 0; i < r->var_count; i++) {
            const Var *v = &r->vars[i];
            const uint32_t *t = def(r, v->pointee);
            if (v->storage != SpvStorageClassInput || !t || op_of(t) != SpvOpTypeArray)
                continue;
            move_var(r, v, SpvStorageClassPrivate, &index, 1, t[2], emu, base, ibi, false);
        }
    }
    for (uint32_t i = 0; i < r->var_count; i++) {
        const Var *v = &r->vars[i];
        if (v->storage != SpvStorageClassInput)
            continue;
        uint32_t value = v->builtin == SpvBuiltInPrimitiveId ? p : v->builtin == SpvBuiltInInvocationId ? inv : 0;
        int kind = scalar_kind(r, v->pointee);
        if (!value || kind < 0)
            continue;
        store(r, v->id, kind == K_UINT ? value : E(r, SpvOpBitcast, r->t_scalar[kind], value));
    }
    function_end(r);

    /* a vertex to its record and its index to the strip, while there is room. */
    c.emit = function_begin(r);
    uint32_t n = load(r, U(r), g_n);
    uint32_t end = if_begin(r, E(r, SpvOpULessThan, r->t_bool, n, uconst(r, M)));
    uint32_t invocation = load(r, U(r), g_inv);
    uint32_t record = add(r, mul(r, invocation, uconst(r, M)), n);
    uint32_t base = add(r, header(r, VKM_GS_OUT_BASE), mul(r, record, uconst(r, out_stride)));
    for (uint32_t i = 0; i < r->var_count; i++) {
        const Var *v = &r->vars[i];
        if (v->storage == SpvStorageClassOutput)
            move_var(r, v, SpvStorageClassPrivate, nullptr, 0, v->pointee, emu, base, obi, true);
    }
    if (strip) {
        uint32_t k = load(r, U(r), g_k);
        write_word(r, emu, add(r, header(r, VKM_GS_IDX_BASE), add(r, mul(r, invocation, uconst(r, idx_count)), k)),
                   record);
        store(r, g_k, add(r, k, uconst(r, 1)));
    }
    store(r, g_n, add(r, n, uconst(r, 1)));
    if_end(r, end);
    function_end(r);

    c.cut = function_begin(r);
    if (strip) {
        uint32_t k = load(r, U(r), g_k);
        uint32_t cut_end = if_begin(r, E(r, SpvOpULessThan, r->t_bool, k, uconst(r, idx_count)));
        uint32_t at = add(r, header(r, VKM_GS_IDX_BASE),
                          add(r, mul(r, load(r, U(r), g_inv), uconst(r, idx_count)), k));
        write_word(r, emu, at, uconst(r, ~0u));
        store(r, g_k, add(r, k, uconst(r, 1)));
        if_end(r, cut_end);
    }
    function_end(r);

    /* the rest of the strip is restarts; points not emitted are put where nothing draws them. */
    c.epilogue = function_begin(r);
    Loop l;
    if (strip) {
        uint32_t k = loop_begin(r, &l, g_k, uconst(r, idx_count));
        uint32_t at = add(r, header(r, VKM_GS_IDX_BASE),
                          add(r, mul(r, load(r, U(r), g_inv), uconst(r, idx_count)), k));
        write_word(r, emu, at, uconst(r, ~0u));
        loop_end(r, &l);
    } else {
        write_word(r, emu, add(r, header(r, VKM_GS_COUNT_BASE), load(r, U(r), g_inv)), load(r, U(r), g_n));
        uint32_t k = loop_begin(r, &l, g_n, uconst(r, M));
        uint32_t rec = add(r, mul(r, load(r, U(r), g_inv), uconst(r, M)), k);
        uint32_t at = add(r, header(r, VKM_GS_OUT_BASE), add(r, mul(r, rec, uconst(r, out_stride)), obi));
        for (uint32_t i = 0; i < 4; i++)
            write_word(r, emu, add(r, at, uconst(r, i)), uconst(r, 0x7fc00000));
        loop_end(r, &l);
    }
    function_end(r);
    return write_module(r, &c, &info->code, &info->words);
}

bool vkm_spirv_capture(const uint32_t *spirv, size_t words, uint32_t **out, size_t *out_words, uint32_t *record_words,
                       uint32_t *builtins)
{
    Rw r = {};
    bool ok = parse(&r, spirv, words) && r.model == SpvExecutionModelVertex;
    if (ok) {
        types_init(&r);
        ok = capture(&r, out, out_words, record_words, builtins);
    }
    rw_free(&r);
    return ok;
}

/* the outputs stream output writes, where they are in the stage's records. */
bool vkm_spirv_xfb(const uint32_t *spirv, size_t words, VkmXfb *out)
{
    memset(out, 0, sizeof(*out));
    Rw r = {};
    bool ok = parse(&r, spirv, words);
    if (ok) {
        uint32_t *buffer = calloc(r.bound, 4), *offset = calloc(r.bound, 4), *has = calloc(r.bound, 1);
        ok = buffer && offset && has;
        for (size_t at = 5; ok && at < words;) {
            const uint32_t *w = spirv + at;
            if (op_of(w) == SpvOpDecorate && w[1] < r.bound && len_of(w) >= 4) {
                if (w[2] == SpvDecorationXfbBuffer) {
                    buffer[w[1]] = w[3];
                    ((uint8_t *)has)[w[1]] = 1;
                } else if (w[2] == SpvDecorationOffset) {
                    offset[w[1]] = w[3];
                }
            }
            if (op_of(w) == SpvOpFunction)
                break;
            at += len_of(w);
        }
        for (size_t at = 5; ok && at < words;) {
            const uint32_t *w = spirv + at;
            if (op_of(w) == SpvOpDecorate && w[1] < r.bound && len_of(w) >= 4 && w[2] == SpvDecorationXfbStride &&
                ((uint8_t *)has)[w[1]] && buffer[w[1]] < 4)
                out->strides[buffer[w[1]]] = w[3];
            if (op_of(w) == SpvOpFunction)
                break;
            at += len_of(w);
        }
        for (uint32_t i = 0; ok && i < r.var_count; i++) {
            const Var *v = &r.vars[i];
            if (v->storage != SpvStorageClassOutput || !((uint8_t *)has)[v->id] || v->location < 0 ||
                buffer[v->id] >= 4 || out->count >= MIME_ARRAY_COUNT(out->entries))
                continue;
            uint32_t n = locations(&r, v->pointee, true);
            out->entries[out->count++] = (typeof(out->entries[0])){buffer[v->id], offset[v->id],
                                                                   (uint32_t)v->location * 4 + v->component, n};
        }
        free(buffer);
        free(offset);
        free(has);
    }
    rw_free(&r);
    return ok && out->count;
}

/* a fragment shader adding one to the word its buffer starts with, each invocation. */
bool vkm_spirv_count(const uint32_t *spirv, size_t words, uint32_t **out, size_t *out_words)
{
    Rw r = {};
    bool ok = parse(&r, spirv, words) && r.model == SpvExecutionModelFragment;
    if (ok) {
        types_init(&r);
        uint32_t counter = storage_buffer(&r, 0);
        r.sb_uint = ptr(&r, SpvStorageClassStorageBuffer, U(&r));
        Calls c = {.model = SpvExecutionModelFragment};
        c.prologue = function_begin(&r);
        E(&r, SpvOpAtomicIAdd, U(&r), word_ptr(&r, counter, uconst(&r, 0)), uconst(&r, SpvScopeDevice), uconst(&r, 0),
          uconst(&r, 1));
        function_end(&r);
        ok = write_module(&r, &c, out, out_words);
    }
    rw_free(&r);
    return ok;
}

bool vkm_spirv_geometry(const uint32_t *spirv, size_t words, bool replay, VkmGsInfo *info)
{
    Rw r = {};
    bool ok = parse(&r, spirv, words);
    if (ok) {
        types_init(&r);
        ok = geometry(&r, replay, info);
    }
    rw_free(&r);
    return ok;
}
