#include "vkm.h"

#define SPV_ENABLE_UTILITY_CODE
#include "spirv.h"

/* the header's inline function, defined here for the files that take it. */
extern inline void SpvHasResultAndType(SpvOp opcode, bool *has_result, bool *has_type);

#include "raytrace.h"

/*
 * a ray tracing pipeline as one compute kernel, linked from its stages' SPIR-V: metal has ray queries, not ray
 * tracing pipelines. each stage becomes a function; its built-ins, payloads, hit attributes and callable data become
 * private variables the kernel fills before calling it and reads back after. a trace is a function walking a ray
 * query: candidates call the any-hit or intersection function of their hit group, and the closest hit or miss
 * function runs once the walk ends. groups are found by the id their handle holds, read from the shader binding
 * table as vulkan lays it out. recursion is unrolled to the pipeline's depth: a stage that traces gets a copy for
 * each level it runs at, and its traces call the next level's functions. callable shaders calling callable shaders
 * are unrolled the same way, VKM_RT_CALLABLE_DEPTH deep.
 */

/* ------------------------------------------------------------------ words. */

typedef struct Buf {
    uint32_t *w;
    size_t n, cap;
    bool oom;
} Buf;

static void put(Buf *b, uint32_t v)
{
    if (b->n == b->cap) {
        size_t cap = MIME_MAX(b->cap * 2, (size_t)256);
        uint32_t *grown = realloc(b->w, cap * 4);
        if (!grown) {
            b->oom = true;
            return;
        }
        b->w = grown;
        b->cap = cap;
    }
    b->w[b->n++] = v;
}

static void put_all(Buf *b, const uint32_t *v, size_t n)
{
    for (size_t i = 0; i < n; i++)
        put(b, v[i]);
}

static void emit(Buf *b, uint32_t op, const uint32_t *ops, uint32_t n)
{
    put(b, (n + 1) << 16 | op);
    put_all(b, ops, n);
}

#define E(b, op, ...) emit(b, op, (uint32_t[]){__VA_ARGS__}, sizeof((uint32_t[]){__VA_ARGS__}) / 4)

static void string_words(Buf *b, const char *s)
{
    size_t len = strlen(s) + 1;
    for (size_t i = 0; i < len; i += 4) {
        uint32_t w = 0;
        for (size_t k = 0; k < 4 && i + k < len; k++)
            w |= (uint32_t)(uint8_t)s[i + k] << (8 * k);
        put(b, w);
    }
}

/* ------------------------------------------------------------------ the stages. */

enum {
    BI_LAUNCH_ID,
    BI_LAUNCH_SIZE,
    BI_WORLD_ORIGIN,
    BI_WORLD_DIRECTION,
    BI_OBJECT_ORIGIN,
    BI_OBJECT_DIRECTION,
    BI_TMIN,
    BI_TMAX,
    BI_CUSTOM_INDEX,
    BI_INSTANCE_ID,
    BI_PRIMITIVE_ID,
    BI_GEOMETRY_INDEX,
    BI_HIT_KIND,
    BI_OBJECT_TO_WORLD,
    BI_WORLD_TO_OBJECT,
    BI_RAY_FLAGS,
    BI_CULL_MASK,
    BI_COUNT,
};

static int rt_builtin(uint32_t b)
{
    switch (b) {
    case SpvBuiltInLaunchIdKHR: return BI_LAUNCH_ID;
    case SpvBuiltInLaunchSizeKHR: return BI_LAUNCH_SIZE;
    case SpvBuiltInWorldRayOriginKHR: return BI_WORLD_ORIGIN;
    case SpvBuiltInWorldRayDirectionKHR: return BI_WORLD_DIRECTION;
    case SpvBuiltInObjectRayOriginKHR: return BI_OBJECT_ORIGIN;
    case SpvBuiltInObjectRayDirectionKHR: return BI_OBJECT_DIRECTION;
    case SpvBuiltInRayTminKHR: return BI_TMIN;
    case SpvBuiltInRayTmaxKHR: return BI_TMAX;
    case SpvBuiltInInstanceCustomIndexKHR: return BI_CUSTOM_INDEX;
    case SpvBuiltInInstanceId: return BI_INSTANCE_ID;
    case SpvBuiltInPrimitiveId: return BI_PRIMITIVE_ID;
    case SpvBuiltInRayGeometryIndexKHR: return BI_GEOMETRY_INDEX;
    case SpvBuiltInHitKindKHR: return BI_HIT_KIND;
    case SpvBuiltInObjectToWorldKHR: return BI_OBJECT_TO_WORLD;
    case SpvBuiltInWorldToObjectKHR: return BI_WORLD_TO_OBJECT;
    case SpvBuiltInIncomingRayFlagsKHR: return BI_RAY_FLAGS;
    case SpvBuiltInCullMaskKHR: return BI_CULL_MASK;
    default: return -1;
    }
}

/* a stage's module, parsed. */
typedef struct Src {
    const uint32_t *w;
    size_t n;
    uint32_t bound, version;
    VkShaderStageFlagBits stage;
    const VkSpecializationInfo *spec;
    uint32_t entry;              /* the entry point's function. */
    size_t globals, functions;   /* the first instruction of the types and of the functions. */
    uint32_t *deco_head;         /* per id, the first decoration targeting it, 1-based, then a chain. */
    uint32_t *deco_next;         /* per decoration. */
    size_t *decos;               /* the decorations' word offsets. */
    uint32_t deco_count;
    uint32_t *keep_import;       /* per id of an OpExtInstImport: 1 for GLSL.std.450, 2 for the ones dropped. */
    bool traces;                 /* the module traces rays or calls callables, so it has a copy a level. */
    bool vulkan_memory_model;
} Src;

static uint32_t deco_value(const Src *s, uint32_t id, uint32_t decoration, bool *found)
{
    for (uint32_t d = s->deco_head[id]; d; d = s->deco_next[d - 1]) {
        const uint32_t *w = s->w + s->decos[d - 1];
        if ((w[0] & 0xffff) == SpvOpDecorate && w[2] == decoration) {
            if (found)
                *found = true;
            return (w[0] >> 16) > 3 ? w[3] : 0;
        }
    }
    if (found)
        *found = false;
    return 0;
}

static bool parse(Src *s, const VkmRtStage *stage)
{
    memset(s, 0, sizeof(*s));
    s->w = stage->code;
    s->n = stage->words;
    s->stage = stage->stage;
    s->spec = stage->spec;
    if (s->n < 5 || s->w[0] != SpvMagicNumber)
        return false;
    s->version = s->w[1];
    s->bound = s->w[3];
    s->deco_head = vkm_zalloc(sizeof(uint32_t) * s->bound);
    s->keep_import = vkm_zalloc(sizeof(uint32_t) * s->bound);
    uint32_t capacity = 64;
    s->decos = malloc(sizeof(size_t) * capacity);
    s->deco_next = malloc(sizeof(uint32_t) * capacity);
    if (!s->deco_head || !s->keep_import || !s->decos || !s->deco_next)
        return false;
    uint32_t model = 0;
    switch (stage->stage) {
    case VK_SHADER_STAGE_RAYGEN_BIT_KHR: model = SpvExecutionModelRayGenerationKHR; break;
    case VK_SHADER_STAGE_ANY_HIT_BIT_KHR: model = SpvExecutionModelAnyHitKHR; break;
    case VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR: model = SpvExecutionModelClosestHitKHR; break;
    case VK_SHADER_STAGE_MISS_BIT_KHR: model = SpvExecutionModelMissKHR; break;
    case VK_SHADER_STAGE_INTERSECTION_BIT_KHR: model = SpvExecutionModelIntersectionKHR; break;
    case VK_SHADER_STAGE_CALLABLE_BIT_KHR: model = SpvExecutionModelCallableKHR; break;
    default: return false;
    }
    for (size_t at = 5; at < s->n;) {
        const uint32_t *w = s->w + at;
        uint32_t op = w[0] & 0xffff, count = w[0] >> 16;
        if (!count || at + count > s->n)
            return false;
        switch (op) {
        case SpvOpEntryPoint:
            if (w[1] == model && !strcmp((const char *)&w[3], stage->entry ? stage->entry : "main"))
                s->entry = w[2];
            break;
        case SpvOpMemoryModel: s->vulkan_memory_model = w[2] == SpvMemoryModelVulkan; break;
        case SpvOpExtInstImport: {
            const char *name = (const char *)&w[2];
            s->keep_import[w[1]] = !strcmp(name, "GLSL.std.450") ? 1 : 2;
            break;
        }
        case SpvOpDecorate:
        case SpvOpMemberDecorate:
        case SpvOpDecorateId:
        case SpvOpDecorateString:
        case SpvOpMemberDecorateString:
            if (w[1] >= s->bound)
                return false;
            if (s->deco_count == capacity) {
                capacity *= 2;
                size_t *d = realloc(s->decos, sizeof(size_t) * capacity);
                uint32_t *n = d ? realloc(s->deco_next, sizeof(uint32_t) * capacity) : nullptr;
                if (d)
                    s->decos = d;
                if (!n)
                    return false;
                s->deco_next = n;
            }
            s->decos[s->deco_count] = at;
            s->deco_next[s->deco_count] = s->deco_head[w[1]];
            s->deco_head[w[1]] = ++s->deco_count;
            break;
        case SpvOpTraceRayKHR:
        case SpvOpExecuteCallableKHR: s->traces = true; break;
        case SpvOpFunction:
            if (!s->functions)
                s->functions = at;
            break;
        default:
            if (!s->globals && !s->functions &&
                ((op >= SpvOpTypeVoid && op <= SpvOpTypeForwardPointer) || op == SpvOpTypeAccelerationStructureKHR ||
                 op == SpvOpTypeRayQueryKHR || (op >= SpvOpConstantTrue && op <= SpvOpSpecConstantOp) ||
                 op == SpvOpVariable || op == SpvOpUndef))
                s->globals = at;
            break;
        }
        at += count;
    }
    if (!s->globals)
        s->globals = s->functions;
    return s->entry && s->functions;
}

static void src_free(Src *s)
{
    free(s->deco_head);
    free(s->deco_next);
    free(s->decos);
    free(s->keep_import);
}

/* ------------------------------------------------------------------ the linked module. */

typedef struct Info {
    uint32_t op;   /* of a type. */
    uint32_t a, b; /* a type's operands: widths, components, storage classes, pointees. */
    uint32_t at;   /* a type's instruction in the types, 1-based. */
    uint32_t type; /* of a value. */
} Info;

/* a stage function as one level or one payload type needs it. */
typedef struct Copy {
    uint32_t stage, level, payload; /* the trace's payload type, for intersection stages. */
    uint32_t fn;
    uint32_t in, attr, record, record_type; /* private variables; record_type is the shader record's pointer. */
    uint32_t builtin[BI_COUNT];
} Copy;

/* a function the kernel makes, made once its body can be. */
enum { GEN_TRACE, GEN_REPORT, GEN_CALLABLE };

/*
 * a level is the trace's in its low byte and, above, how many callable shaders deep the function runs: no function
 * of a level calls one of its own or of a lower one, so nothing recurses.
 */
static constexpr uint32_t TRACE_LEVELS = 0xff;
static constexpr uint32_t CALLABLE_LEVEL = 0x100;

typedef struct Gen {
    uint32_t kind, level, type, fn;
    bool made;
} Gen;

typedef struct Level {
    uint32_t query, flags, cull, tmax, best, kind, terminate, group;
} Level;

typedef struct Lnk {
    uint32_t bound;
    Info *info;
    uint32_t info_cap;
    Buf types, decos, funcs;
    uint32_t caps[64], cap_count;
    bool oom, failed;
    /* interned types and constants. */
    uint32_t *keys;
    size_t key_words, key_cap;
    struct Slot {
        uint64_t hash;
        uint32_t at, len, id;
    } *slots;
    uint32_t slot_cap, slot_count;
    uint32_t glsl;
    /* the stages and groups. */
    Src *srcs;
    const VkmRtStage *stages;
    uint32_t stage_count;
    const VkmRtGroup *groups;
    uint32_t group_count, depth;
    bool any_hit; /* a group has an any-hit shader. */
    Copy **copies;
    uint32_t copy_count, copy_cap;
    Gen *gens;
    uint32_t gen_count, gen_cap;
    /* per level of recursion, its ray's state; per level and payload type, the payload. */
    Level levels[VKM_RT_MAX_DEPTH + 1];
    struct {
        uint32_t level, type, var;
    } payloads[64];
    uint32_t payload_count;
    /* the kernel's state. */
    uint32_t params, status, launch_id, launch_size, global_id;
    uint32_t sbt[7]; /* raygen, miss, hit, callable addresses and miss, hit, callable strides. */
    Buf interface;
} Lnk;

static uint32_t fresh(Lnk *l)
{
    uint32_t id = l->bound++;
    if (id >= l->info_cap) {
        uint32_t cap = MIME_MAX(l->info_cap * 2, 4096u);
        Info *grown = realloc(l->info, sizeof(Info) * cap);
        if (!grown) {
            l->oom = true;
            return id;
        }
        memset(grown + l->info_cap, 0, sizeof(Info) * (cap - l->info_cap));
        l->info = grown;
        l->info_cap = cap;
    }
    return id;
}

static Info *info(Lnk *l, uint32_t id)
{
    static Info none;
    return id < l->info_cap ? &l->info[id] : &none;
}

static void capability(Lnk *l, uint32_t cap)
{
    for (uint32_t i = 0; i < l->cap_count; i++)
        if (l->caps[i] == cap)
            return;
    if (l->cap_count < MIME_ARRAY_COUNT(l->caps))
        l->caps[l->cap_count++] = cap;
}

static void interface(Lnk *l, uint32_t var)
{
    put(&l->interface, var);
}

/* the id of what the key describes, made by make the first time; *made says when it was. */
static uint32_t intern(Lnk *l, const uint32_t *key, uint32_t len, bool *made)
{
    if (made)
        *made = false;
    if (l->slot_count * 2 >= l->slot_cap) {
        uint32_t cap = MIME_MAX(l->slot_cap * 2, 1024u);
        struct Slot *grown = vkm_zalloc(sizeof(*grown) * cap);
        if (!grown) {
            l->oom = true;
            return 0;
        }
        for (uint32_t i = 0; i < l->slot_cap; i++)
            if (l->slots[i].id) {
                uint32_t at = (uint32_t)l->slots[i].hash & (cap - 1);
                while (grown[at].id)
                    at = (at + 1) & (cap - 1);
                grown[at] = l->slots[i];
            }
        free(l->slots);
        l->slots = grown;
        l->slot_cap = cap;
    }
    uint64_t hash = vkm_hash(key, len * 4, 7);
    uint32_t at = (uint32_t)hash & (l->slot_cap - 1);
    while (l->slots[at].id) {
        struct Slot *s = &l->slots[at];
        if (s->hash == hash && s->len == len && !memcmp(l->keys + s->at, key, len * 4))
            return s->id;
        at = (at + 1) & (l->slot_cap - 1);
    }
    if (l->key_words + len > l->key_cap) {
        size_t cap = MIME_MAX(l->key_cap * 2, l->key_words + len + 4096);
        uint32_t *grown = realloc(l->keys, cap * 4);
        if (!grown) {
            l->oom = true;
            return 0;
        }
        l->keys = grown;
        l->key_cap = cap;
    }
    memcpy(l->keys + l->key_words, key, len * 4);
    uint32_t id = fresh(l);
    l->slots[at] = (struct Slot){hash, (uint32_t)l->key_words, len, id};
    l->key_words += len;
    l->slot_count++;
    if (made)
        *made = true;
    return id;
}

/* a type of the given operands, its id made once. */
static uint32_t type_of(Lnk *l, uint32_t op, const uint32_t *ops, uint32_t n)
{
    uint32_t key[64];
    if (n + 2 > MIME_ARRAY_COUNT(key)) {
        l->failed = true;
        return 0;
    }
    key[0] = op;
    key[1] = 0;
    memcpy(key + 2, ops, n * 4);
    bool made;
    uint32_t id = intern(l, key, n + 2, &made);
    if (made) {
        info(l, id)->at = (uint32_t)l->types.n + 1;
        put(&l->types, (n + 2) << 16 | op);
        put(&l->types, id);
        put_all(&l->types, ops, n);
        Info *i = info(l, id);
        i->op = op;
        i->a = n > 0 ? ops[0] : 0;
        i->b = n > 1 ? ops[1] : 0;
    }
    return id;
}

#define T(l, op, ...) type_of(l, op, (uint32_t[]){__VA_ARGS__}, sizeof((uint32_t[]){__VA_ARGS__}) / 4)

static uint32_t t_void(Lnk *l) { return type_of(l, SpvOpTypeVoid, nullptr, 0); }
static uint32_t t_bool(Lnk *l) { return type_of(l, SpvOpTypeBool, nullptr, 0); }
static uint32_t t_u32(Lnk *l) { return T(l, SpvOpTypeInt, 32, 0); }
static uint32_t t_u64(Lnk *l) { return T(l, SpvOpTypeInt, 64, 0); }
static uint32_t t_f32(Lnk *l) { return T(l, SpvOpTypeFloat, 32); }
static uint32_t t_vec(Lnk *l, uint32_t c, uint32_t n) { return T(l, SpvOpTypeVector, c, n); }
static uint32_t t_ptr(Lnk *l, uint32_t sc, uint32_t to) { return T(l, SpvOpTypePointer, sc, to); }

/* a constant of the type and operands, made once. */
static uint32_t const_of(Lnk *l, uint32_t op, uint32_t type, const uint32_t *ops, uint32_t n)
{
    uint32_t key[64];
    if (n + 3 > MIME_ARRAY_COUNT(key)) {
        l->failed = true;
        return 0;
    }
    key[0] = op;
    key[1] = 1;
    key[2] = type;
    memcpy(key + 3, ops, n * 4);
    bool made;
    uint32_t id = intern(l, key, n + 3, &made);
    if (made) {
        put(&l->types, (n + 3) << 16 | op);
        put(&l->types, type);
        put(&l->types, id);
        put_all(&l->types, ops, n);
        info(l, id)->type = type;
    }
    return id;
}

static uint32_t c_u32(Lnk *l, uint32_t v) { return const_of(l, SpvOpConstant, t_u32(l), &v, 1); }

static uint32_t c_u64(Lnk *l, uint64_t v)
{
    uint32_t w[2] = {(uint32_t)v, (uint32_t)(v >> 32)};
    return const_of(l, SpvOpConstant, t_u64(l), w, 2);
}

static uint32_t c_bool(Lnk *l, bool v)
{
    return const_of(l, v ? SpvOpConstantTrue : SpvOpConstantFalse, t_bool(l), nullptr, 0);
}

static uint32_t undef(Lnk *l, uint32_t type)
{
    return const_of(l, SpvOpUndef, type, nullptr, 0);
}

/* a private variable of the kernel. */
static uint32_t private_var(Lnk *l, uint32_t type)
{
    uint32_t ptr = t_ptr(l, SpvStorageClassPrivate, type), id = fresh(l);
    E(&l->types, SpvOpVariable, ptr, id, SpvStorageClassPrivate);
    info(l, id)->type = ptr;
    interface(l, id);
    return id;
}

/* ------------------------------------------------------------------ functions the kernel makes. */

typedef struct Fn {
    Lnk *l;
    Buf head, vars, body;
    uint32_t ret;
} Fn;

static void fn_begin(Fn *f, Lnk *l, uint32_t id, uint32_t ret, const uint32_t *params, uint32_t *param_ids,
                     uint32_t n)
{
    memset(f, 0, sizeof(*f));
    f->l = l;
    f->ret = ret;
    uint32_t ops[16] = {ret};
    memcpy(ops + 1, params, n * 4);
    uint32_t type = type_of(l, SpvOpTypeFunction, ops, n + 1);
    E(&f->head, SpvOpFunction, ret, id, SpvFunctionControlMaskNone, type);
    for (uint32_t i = 0; i < n; i++) {
        param_ids[i] = fresh(l);
        info(l, param_ids[i])->type = params[i];
        E(&f->head, SpvOpFunctionParameter, params[i], param_ids[i]);
    }
    E(&f->head, SpvOpLabel, fresh(l));
}

static void fn_end(Fn *f)
{
    Buf *out = &f->l->funcs;
    put_all(out, f->head.w, f->head.n);
    put_all(out, f->vars.w, f->vars.n);
    put_all(out, f->body.w, f->body.n);
    E(out, SpvOpFunctionEnd);
    if (f->head.oom || f->vars.oom || f->body.oom)
        f->l->oom = true;
    free(f->head.w);
    free(f->vars.w);
    free(f->body.w);
}

/* a value: its instruction emitted into the function's body. */
static uint32_t result_of(Fn *f, uint32_t op, uint32_t type, const uint32_t *ops, uint32_t n)
{
    uint32_t id = fresh(f->l);
    put(&f->body, (n + 3) << 16 | op);
    put(&f->body, type);
    put(&f->body, id);
    put_all(&f->body, ops, n);
    info(f->l, id)->type = type;
    return id;
}

#define V(f, op, type, ...) result_of(f, op, type, (uint32_t[]){__VA_ARGS__}, sizeof((uint32_t[]){__VA_ARGS__}) / 4)
#define S(f, op, ...) E(&(f)->body, op, __VA_ARGS__)

static uint32_t type_of_value(Lnk *l, uint32_t id) { return info(l, id)->type; }
static uint32_t pointee(Lnk *l, uint32_t ptr_type) { return info(l, ptr_type)->b; }

static uint32_t load(Fn *f, uint32_t ptr)
{
    return V(f, SpvOpLoad, pointee(f->l, type_of_value(f->l, ptr)), ptr);
}

static void store(Fn *f, uint32_t ptr, uint32_t value) { S(f, SpvOpStore, ptr, value); }

static void label(Fn *f, uint32_t id) { S(f, SpvOpLabel, id); }

/* an if without an else: its body is written between if_begin and if_end. */
static uint32_t if_begin(Fn *f, uint32_t cond)
{
    uint32_t then = fresh(f->l), merge = fresh(f->l);
    S(f, SpvOpSelectionMerge, merge, SpvSelectionControlMaskNone);
    S(f, SpvOpBranchConditional, cond, then, merge);
    label(f, then);
    return merge;
}

static void if_end(Fn *f, uint32_t merge)
{
    S(f, SpvOpBranch, merge);
    label(f, merge);
}

/* a switch on a u32 over cases, each written by the caller after case_begin, and its merge. */
typedef struct Switch {
    uint32_t merge, count;
    uint32_t values[512], labels[512];
    size_t at; /* where the OpSwitch goes. */
} Switch;

/* the u32 value a stage's built-in needs, converted from what it is. */
static uint32_t as_type(Fn *f, uint32_t value, uint32_t type)
{
    if (type_of_value(f->l, value) == type)
        return value;
    return V(f, SpvOpBitcast, type, value);
}

/* ------------------------------------------------------------------ stage copies. */

static uint32_t request(Lnk *l, uint32_t kind, uint32_t level, uint32_t type)
{
    for (uint32_t i = 0; i < l->gen_count; i++)
        if (l->gens[i].kind == kind && l->gens[i].level == level && l->gens[i].type == type)
            return l->gens[i].fn;
    if (l->gen_count == l->gen_cap) {
        uint32_t cap = MIME_MAX(l->gen_cap * 2, 16u);
        Gen *grown = realloc(l->gens, sizeof(Gen) * cap);
        if (!grown) {
            l->oom = true;
            return 0;
        }
        l->gens = grown;
        l->gen_cap = cap;
    }
    uint32_t fn = fresh(l);
    l->gens[l->gen_count++] = (Gen){kind, level, type, fn, false};
    return fn;
}

/* whether word i of the instruction is a literal, not an id, past its result type and id. */
static bool literal(const uint32_t *w, uint32_t i)
{
    uint32_t op = w[0] & 0xffff;
    switch (op) {
    case SpvOpExtInst: return i == 4;
    case SpvOpCompositeExtract: return i >= 4;
    case SpvOpCompositeInsert: return i >= 5;
    case SpvOpVectorShuffle: return i >= 5;
    case SpvOpFunction: return i == 3;
    case SpvOpVariable: return i == 3;
    case SpvOpArrayLength: return i == 4;
    case SpvOpLoopMerge: return i >= 3;
    case SpvOpSelectionMerge: return i >= 2;
    case SpvOpBranchConditional: return i >= 4;
    case SpvOpLifetimeStart:
    case SpvOpLifetimeStop: return i == 2;
    case SpvOpSDot:
    case SpvOpUDot:
    case SpvOpSUDot: return i == 5;
    case SpvOpSDotAccSat:
    case SpvOpUDotAccSat:
    case SpvOpSUDotAccSat: return i == 6;
    case SpvOpGroupIAdd:
    case SpvOpGroupFAdd:
    case SpvOpGroupFMin:
    case SpvOpGroupUMin:
    case SpvOpGroupSMin:
    case SpvOpGroupFMax:
    case SpvOpGroupUMax:
    case SpvOpGroupSMax:
    case SpvOpGroupNonUniformBallotBitCount: return i == 4;
    default: break;
    }
    if (op >= SpvOpGroupNonUniformIAdd && op <= SpvOpGroupNonUniformLogicalXor)
        return i == 4;
    /* memory and image operands: a mask, then ids, but for memory's alignment. */
    uint32_t mask_at = 0;
    bool memory = false;
    switch (op) {
    case SpvOpLoad: mask_at = 4, memory = true; break;
    case SpvOpStore:
    case SpvOpCopyMemory: mask_at = 3, memory = true; break;
    case SpvOpCopyMemorySized: mask_at = 4, memory = true; break;
    case SpvOpImageWrite: mask_at = 4; break;
    case SpvOpImageSampleImplicitLod:
    case SpvOpImageSampleExplicitLod:
    case SpvOpImageSampleProjImplicitLod:
    case SpvOpImageSampleProjExplicitLod:
    case SpvOpImageFetch:
    case SpvOpImageRead:
    case SpvOpImageSparseSampleImplicitLod:
    case SpvOpImageSparseSampleExplicitLod:
    case SpvOpImageSparseSampleProjImplicitLod:
    case SpvOpImageSparseSampleProjExplicitLod:
    case SpvOpImageSparseFetch:
    case SpvOpImageSparseRead: mask_at = 5; break;
    case SpvOpImageSampleDrefImplicitLod:
    case SpvOpImageSampleDrefExplicitLod:
    case SpvOpImageSampleProjDrefImplicitLod:
    case SpvOpImageSampleProjDrefExplicitLod:
    case SpvOpImageGather:
    case SpvOpImageDrefGather:
    case SpvOpImageSparseSampleDrefImplicitLod:
    case SpvOpImageSparseSampleDrefExplicitLod:
    case SpvOpImageSparseSampleProjDrefImplicitLod:
    case SpvOpImageSparseSampleProjDrefExplicitLod:
    case SpvOpImageSparseGather:
    case SpvOpImageSparseDrefGather: mask_at = 6; break;
    default: return false;
    }
    if (i < mask_at)
        return false;
    if (i == mask_at)
        return true;
    return memory && (w[mask_at] & SpvMemoryAccessAlignedMask) && i == mask_at + 1;
}

typedef struct Copier {
    Lnk *l;
    const Src *s;
    Copy *c;
    uint32_t *map;
    bool *made; /* per source id, whether its merged id is the copy's own, so its decorations are written. */
    bool *priv; /* per source id, a pointer into a built-in made private. */
    uint32_t record_src;
    uint32_t payload_type;
    uint32_t ret; /* of the function being copied. */
} Copier;

static uint32_t m(const Copier *k, uint32_t id) { return id < k->s->bound ? k->map[id] : 0; }

/* the storage class a stage's variable has in the kernel. */
static uint32_t storage_of(uint32_t sc)
{
    switch (sc) {
    case SpvStorageClassRayPayloadKHR:
    case SpvStorageClassIncomingRayPayloadKHR:
    case SpvStorageClassHitAttributeKHR:
    case SpvStorageClassCallableDataKHR:
    case SpvStorageClassIncomingCallableDataKHR:
    case SpvStorageClassOutput: return SpvStorageClassPrivate;
    case SpvStorageClassShaderRecordBufferKHR: return SpvStorageClassPhysicalStorageBuffer;
    default: return sc;
    }
}

/* the decorations of a type or a variable, as part of its key. */
static uint32_t deco_key(const Copier *k, uint32_t id, uint32_t *key, uint32_t n, uint32_t cap)
{
    const Src *s = k->s;
    for (uint32_t d = s->deco_head[id]; d; d = s->deco_next[d - 1]) {
        const uint32_t *w = s->w + s->decos[d - 1];
        uint32_t count = w[0] >> 16;
        if (n + count >= cap)
            break;
        key[n++] = w[0];
        for (uint32_t i = 2; i < count; i++)
            key[n++] = w[i];
    }
    return n;
}

static void freeze_spec(const Copier *k, const uint32_t *w, uint32_t *op, uint32_t *values, uint32_t *count)
{
    const Src *s = k->s;
    bool has;
    uint32_t spec_id = deco_value(s, w[2], SpvDecorationSpecId, &has);
    const VkSpecializationMapEntry *e = nullptr;
    for (uint32_t i = 0; has && s->spec && i < s->spec->mapEntryCount; i++)
        if (s->spec->pMapEntries[i].constantID == spec_id)
            e = &s->spec->pMapEntries[i];
    uint32_t o = w[0] & 0xffff;
    if (o == SpvOpSpecConstantTrue || o == SpvOpSpecConstantFalse) {
        bool value = o == SpvOpSpecConstantTrue;
        if (e) {
            uint32_t b = 0;
            memcpy(&b, (const uint8_t *)s->spec->pData + e->offset, MIME_MIN(e->size, sizeof(b)));
            value = b != 0;
        }
        *op = value ? SpvOpConstantTrue : SpvOpConstantFalse;
        *count = 0;
        return;
    }
    *op = SpvOpConstant;
    *count = (w[0] >> 16) - 3;
    memcpy(values, w + 3, *count * 4);
    if (e)
        memcpy(values, (const uint8_t *)s->spec->pData + e->offset, MIME_MIN(e->size, (size_t)*count * 4));
}

/* the types, constants and variables of a copy, interned where they are the same as another stage's. */
static bool copy_globals(Copier *k)
{
    Lnk *l = k->l;
    const Src *s = k->s;
    for (size_t at = 5; at < s->globals;) {
        const uint32_t *w = s->w + at;
        if ((w[0] & 0xffff) == SpvOpExtInstImport)
            k->map[w[1]] = s->keep_import[w[1]] == 1 ? l->glsl : 0;
        at += w[0] >> 16;
    }
    for (size_t at = s->globals; at < s->functions;) {
        const uint32_t *w = s->w + at;
        uint32_t op = w[0] & 0xffff, count = w[0] >> 16;
        at += count;
        uint32_t ops[64], n = 0, key[160];
        if (count > MIME_ARRAY_COUNT(ops))
            return false;
        switch (op) {
        case SpvOpLine:
        case SpvOpNoLine:
        case SpvOpExtInst: break;
        case SpvOpTypeForwardPointer: {
            uint32_t id = fresh(l);
            k->map[w[1]] = id;
            E(&l->types, SpvOpTypeForwardPointer, id, storage_of(w[2]));
            info(l, id)->op = SpvOpTypePointer;
            info(l, id)->a = storage_of(w[2]);
            break;
        }
        case SpvOpTypePointer: {
            uint32_t sc = storage_of(w[2]), to = m(k, w[3]);
            if (k->map[w[1]]) {
                /* forward declared, so made once for this copy alone. */
                E(&l->types, SpvOpTypePointer, k->map[w[1]], sc, to);
                info(l, k->map[w[1]])->b = to;
            } else {
                k->map[w[1]] = t_ptr(l, sc, to);
            }
            break;
        }
        default:
            if ((op >= SpvOpTypeVoid && op <= SpvOpTypeFunction) || op == SpvOpTypeAccelerationStructureKHR ||
                op == SpvOpTypeRayQueryKHR) {
                for (uint32_t i = 2; i < count; i++)
                    ops[n++] = op == SpvOpTypeInt || op == SpvOpTypeFloat || (op == SpvOpTypeVector && i == 3) ||
                                       (op == SpvOpTypeMatrix && i == 3) || (op == SpvOpTypeImage && i > 2) ||
                                       op == SpvOpTypeOpaque
                                   ? w[i]
                                   : m(k, w[i]);
                if (op != SpvOpTypeStruct && op != SpvOpTypeArray && op != SpvOpTypeRuntimeArray) {
                    k->map[w[1]] = type_of(l, op, ops, n);
                    break;
                }
                /* aggregates carry their layout decorations in their key. */
                uint32_t len = 0;
                key[len++] = op;
                key[len++] = 2;
                memcpy(key + len, ops, n * 4);
                len += n;
                len = deco_key(k, w[1], key, len, MIME_ARRAY_COUNT(key));
                bool made;
                uint32_t id = intern(l, key, len, &made);
                k->map[w[1]] = id;
                if (made) {
                    info(l, id)->at = (uint32_t)l->types.n + 1;
                    put(&l->types, (n + 2) << 16 | op);
                    put(&l->types, id);
                    put_all(&l->types, ops, n);
                    info(l, id)->op = op;
                    info(l, id)->a = n ? ops[0] : 0;
                    info(l, id)->b = n > 1 ? ops[1] : 0;
                    k->made[w[1]] = true;
                }
                break;
            }
            if (op == SpvOpConstant || op == SpvOpConstantTrue || op == SpvOpConstantFalse || op == SpvOpConstantNull ||
                op == SpvOpConstantComposite || op == SpvOpConstantSampler || op == SpvOpUndef ||
                op == SpvOpSpecConstant || op == SpvOpSpecConstantTrue || op == SpvOpSpecConstantFalse ||
                op == SpvOpSpecConstantComposite) {
                uint32_t o = op;
                if (op == SpvOpSpecConstant || op == SpvOpSpecConstantTrue || op == SpvOpSpecConstantFalse) {
                    freeze_spec(k, w, &o, ops, &n);
                } else {
                    o = op == SpvOpSpecConstantComposite ? SpvOpConstantComposite : op;
                    for (uint32_t i = 3; i < count; i++)
                        ops[n++] = o == SpvOpConstant || o == SpvOpConstantSampler ? w[i] : m(k, w[i]);
                }
                k->map[w[2]] = const_of(l, o, m(k, w[1]), ops, n);
                break;
            }
            if (op == SpvOpSpecConstantOp) {
                uint32_t id = fresh(l);
                k->map[w[2]] = id;
                ops[n++] = m(k, w[1]);
                ops[n++] = id;
                ops[n++] = w[3];
                for (uint32_t i = 4; i < count; i++)
                    ops[n++] = m(k, w[i]);
                emit(&l->types, op, ops, n);
                info(l, id)->type = m(k, w[1]);
                break;
            }
            if (op == SpvOpVariable) {
                uint32_t sc = w[3], id = w[2];
                uint32_t type = pointee(l, m(k, w[1]));
                bool has_builtin;
                uint32_t builtin = deco_value(s, id, SpvDecorationBuiltIn, &has_builtin);
                int rt = has_builtin ? rt_builtin(builtin) : -1;
                uint32_t var = 0;
                if (sc == SpvStorageClassInput && rt >= 0) {
                    var = private_var(l, type);
                    k->c->builtin[rt] = var;
                    k->priv[id] = true;
                } else if (sc == SpvStorageClassInput || sc == SpvStorageClassUniformConstant ||
                           sc == SpvStorageClassUniform || sc == SpvStorageClassStorageBuffer ||
                           sc == SpvStorageClassPushConstant) {
                    /* the same resource, or compute built-in, of every stage is one variable. */
                    uint32_t len = 0;
                    key[len++] = op;
                    key[len++] = 3;
                    key[len++] = m(k, w[1]);
                    key[len++] = sc;
                    if (sc != SpvStorageClassPushConstant)
                        len = deco_key(k, id, key, len, MIME_ARRAY_COUNT(key));
                    bool made;
                    var = intern(l, key, len, &made);
                    if (made) {
                        E(&l->types, SpvOpVariable, m(k, w[1]), var, sc);
                        info(l, var)->type = m(k, w[1]);
                        k->made[id] = true;
                        interface(l, var);
                    }
                } else if (sc == SpvStorageClassShaderRecordBufferKHR) {
                    uint32_t psb = t_ptr(l, SpvStorageClassPhysicalStorageBuffer, type);
                    var = private_var(l, psb);
                    k->c->record = var;
                    k->c->record_type = psb;
                    k->record_src = id;
                    capability(l, SpvCapabilityPhysicalStorageBufferAddresses);
                } else {
                    var = fresh(l);
                    uint32_t vsc = storage_of(sc), ptr = t_ptr(l, vsc, type);
                    if (count > 4)
                        E(&l->types, SpvOpVariable, ptr, var, vsc, m(k, w[4]));
                    else
                        E(&l->types, SpvOpVariable, ptr, var, vsc);
                    info(l, var)->type = ptr;
                    k->made[id] = true;
                    interface(l, var);
                    if (sc == SpvStorageClassIncomingRayPayloadKHR || sc == SpvStorageClassIncomingCallableDataKHR)
                        k->c->in = var;
                    else if (sc == SpvStorageClassHitAttributeKHR)
                        k->c->attr = var;
                }
                k->map[id] = var;
                break;
            }
            /* what else is here would not survive the copy. */
            VKM_ERROR("ray tracing: a stage has global instruction %u", op);
            return false;
        }
    }
    return true;
}

/* the decorations of what the copy made, without those of the stages' interfaces. */
static void copy_decorations(Copier *k)
{
    Lnk *l = k->l;
    const Src *s = k->s;
    for (uint32_t d = 0; d < s->deco_count; d++) {
        const uint32_t *w = s->w + s->decos[d];
        uint32_t op = w[0] & 0xffff, count = w[0] >> 16, target = w[1];
        if (target >= s->bound || !k->made[target] || !k->map[target])
            continue;
        uint32_t decoration = op == SpvOpMemberDecorate || op == SpvOpMemberDecorateString ? w[3] : w[2];
        if (decoration == SpvDecorationSpecId || decoration == SpvDecorationLocation ||
            decoration == SpvDecorationComponent || decoration == SpvDecorationHitObjectShaderRecordBufferNV)
            continue;
        if (decoration == SpvDecorationBuiltIn && rt_builtin(w[3]) >= 0)
            continue;
        uint32_t ops[64];
        if (count > MIME_ARRAY_COUNT(ops))
            continue;
        ops[0] = k->map[target];
        for (uint32_t i = 2; i < count; i++)
            ops[i - 1] = op == SpvOpDecorateId && i > 2 ? m(k, w[i]) : w[i];
        emit(&l->decos, op, ops, count - 1);
    }
}

static uint32_t payload_var(Lnk *l, uint32_t level, uint32_t type)
{
    for (uint32_t i = 0; i < l->payload_count; i++)
        if (l->payloads[i].level == level && l->payloads[i].type == type)
            return l->payloads[i].var;
    if (l->payload_count == MIME_ARRAY_COUNT(l->payloads)) {
        l->failed = true;
        return 0;
    }
    uint32_t var = private_var(l, type);
    l->payloads[l->payload_count++] = (typeof(l->payloads[0])){level, type, var};
    return var;
}

/* PhysicalStorageBuffer loads and stores need an alignment. */
static bool physical(Lnk *l, uint32_t ptr)
{
    Info *t = info(l, type_of_value(l, ptr));
    return t->op == SpvOpTypePointer && t->a == SpvStorageClassPhysicalStorageBuffer;
}

/* whether the block can be split after the call before at: not when it is a loop's header, whose merge would move. */
static bool splittable(const Src *s, size_t at)
{
    while (at < s->n) {
        const uint32_t *w = s->w + at;
        switch (w[0] & 0xffff) {
        case SpvOpLoopMerge: return false;
        case SpvOpBranch:
        case SpvOpBranchConditional:
        case SpvOpSwitch:
        case SpvOpReturn:
        case SpvOpReturnValue:
        case SpvOpKill:
        case SpvOpUnreachable:
        case SpvOpIgnoreIntersectionKHR:
        case SpvOpTerminateRayKHR:
        case SpvOpFunctionEnd: return true;
        default: break;
        }
        at += w[0] >> 16;
    }
    return true;
}

static bool copy_functions(Copier *k)
{
    Lnk *l = k->l;
    const Src *s = k->s;
    /* every id the functions define is the copy's own. */
    for (size_t at = s->functions; at < s->n;) {
        const uint32_t *w = s->w + at;
        uint32_t op = w[0] & 0xffff;
        bool has_result, has_type;
        SpvHasResultAndType(op, &has_result, &has_type);
        if (op == SpvOpLabel || has_result) {
            uint32_t id = op == SpvOpLabel ? w[1] : w[has_type ? 2 : 1];
            if (id < s->bound && !k->map[id]) {
                k->map[id] = fresh(l);
                k->made[id] = true;
            }
        }
        at += w[0] >> 16;
    }
    k->c->fn = k->map[s->entry];
    Buf *out = &l->funcs;
    /*
     * an any-hit shader that ignores or ends its ray inside a function returns from every caller too: each call is
     * followed by a return when the status changed. that splits the caller's block, so the phis naming it as a
     * predecessor are renamed to its last part once the function is whole.
     */
    bool exits = s->stage == VK_SHADER_STAGE_ANY_HIT_BIT_KHR;
    Buf phis = {}, splits = {};
    uint32_t block = 0;
    for (size_t at = s->functions; at < s->n;) {
        const uint32_t *w = s->w + at;
        uint32_t op = w[0] & 0xffff, count = w[0] >> 16;
        at += count;
        uint32_t ops[256];
        if (count > MIME_ARRAY_COUNT(ops))
            return false;
        bool has_result, has_type;
        SpvHasResultAndType(op, &has_result, &has_type);
        switch (op) {
        case SpvOpLine:
        case SpvOpNoLine: continue;
        case SpvOpExtInst:
            if (!m(k, w[3]))
                continue;
            break;
        case SpvOpFunction: k->ret = m(k, w[1]); break;
        case SpvOpTraceRayKHR: {
            uint32_t payload = m(k, w[11]);
            uint32_t type = pointee(l, type_of_value(l, payload));
            uint32_t fn = request(l, GEN_TRACE, k->c->level & TRACE_LEVELS, type);
            uint32_t u32 = t_u32(l), args[14] = {t_void(l), fresh(l), fn};
            for (uint32_t i = 1; i <= 11; i++)
                args[2 + i] = m(k, w[i]);
            /* the integer operands may be signed: the function takes them unsigned. */
            for (uint32_t i = 2; i <= 6; i++)
                if (type_of_value(l, args[2 + i]) != u32) {
                    uint32_t cast = fresh(l);
                    E(out, SpvOpBitcast, u32, cast, args[2 + i]);
                    info(l, cast)->type = u32;
                    args[2 + i] = cast;
                }
            emit(out, SpvOpFunctionCall, args, 14);
            continue;
        }
        case SpvOpExecuteCallableKHR: {
            uint32_t data = m(k, w[2]), index = m(k, w[1]), u32 = t_u32(l);
            uint32_t fn =
                request(l, GEN_CALLABLE, k->c->level + CALLABLE_LEVEL, pointee(l, type_of_value(l, data)));
            if (type_of_value(l, index) != u32) {
                uint32_t cast = fresh(l);
                E(out, SpvOpBitcast, u32, cast, index);
                info(l, cast)->type = u32;
                index = cast;
            }
            E(out, SpvOpFunctionCall, t_void(l), fresh(l), fn, index, data);
            continue;
        }
        case SpvOpReportIntersectionKHR: {
            uint32_t fn = request(l, GEN_REPORT, k->c->level, k->c->payload);
            uint32_t kind = m(k, w[4]), u32 = t_u32(l);
            if (type_of_value(l, kind) != u32) {
                uint32_t cast = fresh(l);
                E(out, SpvOpBitcast, u32, cast, kind);
                info(l, cast)->type = u32;
                kind = cast;
            }
            E(out, SpvOpFunctionCall, m(k, w[1]), m(k, w[2]), fn, m(k, w[3]), kind);
            info(l, m(k, w[2]))->type = m(k, w[1]);
            continue;
        }
        case SpvOpIgnoreIntersectionKHR:
        case SpvOpTerminateRayKHR:
            E(out, SpvOpStore, l->status,
              c_u32(l, op == SpvOpIgnoreIntersectionKHR ? VKM_RT_IGNORE : VKM_RT_TERMINATE));
            if (k->ret == t_void(l))
                E(out, SpvOpReturn);
            else
                E(out, SpvOpReturnValue, undef(l, k->ret));
            continue;
        default: break;
        }
        if (op == SpvOpSwitch) {
            /* literals of the selector's width between the labels. */
            uint32_t n = 0, width = info(l, type_of_value(l, m(k, w[1])))->a == 64 ? 2 : 1;
            ops[n++] = m(k, w[1]);
            ops[n++] = m(k, w[2]);
            for (uint32_t i = 3; i + width < count + 0u && i < count; i += width + 1) {
                for (uint32_t j = 0; j < width; j++)
                    ops[n++] = w[i + j];
                ops[n++] = m(k, w[i + width]);
            }
            emit(out, op, ops, n);
            continue;
        }
        /* the shader record is a pointer now: its uses take it loaded. */
        uint32_t first = (has_type ? 1 : 0) + (has_result ? 1 : 0) + 1, loaded = 0;
        for (uint32_t i = 1; i < count; i++) {
            if (i < first) {
                ops[i - 1] = m(k, w[i]);
                continue;
            }
            if (literal(w, i)) {
                ops[i - 1] = w[i];
                continue;
            }
            if (k->record_src && w[i] == k->record_src) {
                if (!loaded) {
                    loaded = fresh(l);
                    E(out, SpvOpLoad, k->c->record_type, loaded, k->c->record);
                    info(l, loaded)->type = k->c->record_type;
                }
                ops[i - 1] = loaded;
                continue;
            }
            ops[i - 1] = m(k, w[i]);
        }
        /* a pointer into a private built-in is private too. */
        if ((op == SpvOpAccessChain || op == SpvOpInBoundsAccessChain) && w[3] < s->bound && k->priv[w[3]]) {
            ops[0] = t_ptr(l, SpvStorageClassPrivate, pointee(l, ops[0]));
            if (w[2] < s->bound)
                k->priv[w[2]] = true;
        }
        if (op == SpvOpFunctionParameter && ops[0])
            info(l, ops[1])->type = ops[0];
        uint32_t n = count - 1;
        if ((op == SpvOpLoad || op == SpvOpStore) && physical(l, ops[op == SpvOpLoad ? 2 : 0])) {
            uint32_t mask_at = op == SpvOpLoad ? 3 : 2;
            if (n <= mask_at) {
                ops[n++] = SpvMemoryAccessAlignedMask;
                ops[n++] = 4;
            } else if (!(ops[mask_at] & SpvMemoryAccessAlignedMask)) {
                memmove(ops + mask_at + 2, ops + mask_at + 1, (n - mask_at - 1) * 4);
                ops[mask_at] |= SpvMemoryAccessAlignedMask;
                ops[mask_at + 1] = 4;
                n++;
            }
        }
        if (has_result && has_type)
            info(l, ops[1])->type = ops[0];
        if (op == SpvOpLabel)
            block = ops[0];
        if (op == SpvOpPhi && exits)
            put(&phis, (uint32_t)out->n);
        emit(out, op, ops, n);
        if (op == SpvOpFunctionCall && exits && splittable(s, at)) {
            uint32_t status = fresh(l), changed = fresh(l), leave = fresh(l), rest = fresh(l);
            E(out, SpvOpLoad, t_u32(l), status, l->status);
            E(out, SpvOpINotEqual, t_bool(l), changed, status, c_u32(l, VKM_RT_ACCEPT));
            E(out, SpvOpSelectionMerge, rest, SpvSelectionControlMaskNone);
            E(out, SpvOpBranchConditional, changed, leave, rest);
            E(out, SpvOpLabel, leave);
            if (k->ret == t_void(l))
                E(out, SpvOpReturn);
            else
                E(out, SpvOpReturnValue, undef(l, k->ret));
            E(out, SpvOpLabel, rest);
            put(&splits, block);
            put(&splits, rest);
        }
        if (op == SpvOpFunctionEnd) {
            for (size_t i = 0; i < phis.n && !out->oom; i++) {
                uint32_t *phi = out->w + phis.w[i];
                for (uint32_t j = 4; j < phi[0] >> 16; j += 2)
                    /* the last split of a block is its last part. */
                    for (size_t x = splits.n; x >= 2; x -= 2)
                        if (splits.w[x - 2] == phi[j]) {
                            phi[j] = splits.w[x - 1];
                            break;
                        }
            }
            phis.n = splits.n = 0;
        }
    }
    free(phis.w);
    free(splits.w);
    return true;
}

/*
 * a stage's function for a level, and for intersection stages a payload type too: a stage that does not trace has
 * one for every level.
 */
static Copy *copy_of(Lnk *l, uint32_t stage, uint32_t level, uint32_t payload)
{
    const Src *s = &l->srcs[stage];
    bool per_level = s->traces || s->stage == VK_SHADER_STAGE_INTERSECTION_BIT_KHR;
    if (!per_level)
        level = 0;
    if (s->stage != VK_SHADER_STAGE_INTERSECTION_BIT_KHR)
        payload = 0;
    for (uint32_t i = 0; i < l->copy_count; i++)
        if (l->copies[i]->stage == stage && l->copies[i]->level == level && l->copies[i]->payload == payload)
            return l->copies[i];
    if (l->copy_count == l->copy_cap) {
        uint32_t cap = MIME_MAX(l->copy_cap * 2, 16u);
        Copy **grown = realloc(l->copies, sizeof(Copy *) * cap);
        if (!grown) {
            l->oom = true;
            return nullptr;
        }
        l->copies = grown;
        l->copy_cap = cap;
    }
    Copy *c = vkm_zalloc(sizeof(*c));
    if (!c) {
        l->oom = true;
        return nullptr;
    }
    l->copies[l->copy_count++] = c;
    c->stage = stage;
    c->level = level;
    c->payload = payload;
    Copier k = {l, s, c};
    k.map = vkm_zalloc(sizeof(uint32_t) * s->bound);
    k.made = vkm_zalloc(sizeof(bool) * s->bound);
    k.priv = vkm_zalloc(sizeof(bool) * s->bound);
    k.payload_type = payload;
    bool ok = k.map && k.made && k.priv && copy_globals(&k) && copy_functions(&k);
    if (ok)
        copy_decorations(&k);
    else
        l->failed = true;
    free(k.map);
    free(k.made);
    free(k.priv);
    return ok ? c : nullptr;
}

/* ------------------------------------------------------------------ the kernel's state. */

/* the struct of the parameters vkCmdTraceRays gives, and the kernel's copy of them. */
enum {
    P_RAYGEN,
    P_MISS,
    P_HIT,
    P_CALLABLE,
    P_MISS_STRIDE,
    P_HIT_STRIDE,
    P_CALLABLE_STRIDE,
    P_MODE,
    P_WIDTH,
    P_HEIGHT,
    P_DEPTH,
    P_PAD,
    P_INDIRECT,
    P_COUNT,
};

static uint32_t param_type(Lnk *l, uint32_t member)
{
    return member < P_MISS_STRIDE || member == P_INDIRECT ? t_u64(l) : t_u32(l);
}

static void params_init(Lnk *l)
{
    uint32_t members[P_COUNT];
    for (uint32_t i = 0; i < P_COUNT; i++)
        members[i] = param_type(l, i);
    /* unique: its decorations are its own. */
    uint32_t st = fresh(l);
    put(&l->types, (P_COUNT + 2) << 16 | SpvOpTypeStruct);
    put(&l->types, st);
    put_all(&l->types, members, P_COUNT);
    info(l, st)->op = SpvOpTypeStruct;
    static const uint32_t offsets[P_COUNT] = {0, 8, 16, 24, 32, 36, 40, 44, 48, 52, 56, 60, 64};
    for (uint32_t i = 0; i < P_COUNT; i++)
        E(&l->decos, SpvOpMemberDecorate, st, i, SpvDecorationOffset, offsets[i]);
    E(&l->decos, SpvOpDecorate, st, SpvDecorationBlock);
    uint32_t ptr = t_ptr(l, SpvStorageClassStorageBuffer, st);
    l->params = fresh(l);
    E(&l->types, SpvOpVariable, ptr, l->params, SpvStorageClassStorageBuffer);
    info(l, l->params)->type = ptr;
    E(&l->decos, SpvOpDecorate, l->params, SpvDecorationDescriptorSet, VKM_RT_SET);
    E(&l->decos, SpvOpDecorate, l->params, SpvDecorationBinding, 0);
    E(&l->decos, SpvOpDecorate, l->params, SpvDecorationNonWritable);
    interface(l, l->params);
    l->status = private_var(l, t_u32(l));
    uint32_t uv3 = t_vec(l, t_u32(l), 3);
    l->launch_id = private_var(l, uv3);
    l->launch_size = private_var(l, uv3);
    for (uint32_t i = 0; i < 7; i++)
        l->sbt[i] = private_var(l, i < 4 ? t_u64(l) : t_u32(l));
    uint32_t in = t_ptr(l, SpvStorageClassInput, uv3);
    l->global_id = fresh(l);
    E(&l->types, SpvOpVariable, in, l->global_id, SpvStorageClassInput);
    info(l, l->global_id)->type = in;
    E(&l->decos, SpvOpDecorate, l->global_id, SpvDecorationBuiltIn, SpvBuiltInGlobalInvocationId);
    interface(l, l->global_id);
}

static Level *level_of(Lnk *l, uint32_t level)
{
    Level *v = &l->levels[level];
    if (!v->query) {
        v->query = private_var(l, type_of(l, SpvOpTypeRayQueryKHR, nullptr, 0));
        v->flags = private_var(l, t_u32(l));
        v->cull = private_var(l, t_u32(l));
        v->tmax = private_var(l, t_f32(l));
        v->best = private_var(l, t_f32(l));
        v->kind = private_var(l, t_u32(l));
        v->terminate = private_var(l, t_bool(l));
        v->group = private_var(l, t_u32(l));
    }
    return v;
}

/* a u32 or u64 read from a gpu address. */
static uint32_t load_address(Fn *f, uint32_t address, uint32_t type)
{
    Lnk *l = f->l;
    uint32_t ptr_type = t_ptr(l, SpvStorageClassPhysicalStorageBuffer, type);
    uint32_t ptr = V(f, SpvOpConvertUToPtr, ptr_type, address);
    uint32_t id = fresh(l);
    S(f, SpvOpLoad, type, id, ptr, SpvMemoryAccessAlignedMask, type == t_u64(l) ? 8 : 4);
    info(l, id)->type = type;
    return id;
}

static uint32_t add64(Fn *f, uint32_t a, uint32_t b) { return V(f, SpvOpIAdd, t_u64(f->l), a, b); }

static uint32_t widen(Fn *f, uint32_t u32) { return V(f, SpvOpUConvert, t_u64(f->l), u32); }

/* the record of the table at base: base + index * stride. */
static uint32_t record_at(Fn *f, uint32_t base, uint32_t index, uint32_t stride)
{
    Lnk *l = f->l;
    uint32_t offset = V(f, SpvOpIMul, t_u64(l), widen(f, index), widen(f, stride));
    return add64(f, base, offset);
}

/* the group id the record's handle starts with, or 0 for no record. */
static uint32_t group_at(Fn *f, uint32_t record)
{
    Lnk *l = f->l;
    uint32_t some = V(f, SpvOpINotEqual, t_bool(l), record, c_u64(l, 0));
    uint32_t result = fresh(l);
    uint32_t start = fresh(l), read = fresh(l), merge = fresh(l);
    S(f, SpvOpBranch, start);
    label(f, start);
    S(f, SpvOpSelectionMerge, merge, SpvSelectionControlMaskNone);
    S(f, SpvOpBranchConditional, some, read, merge);
    label(f, read);
    uint32_t id = load_address(f, record, t_u32(l));
    S(f, SpvOpBranch, merge);
    label(f, merge);
    S(f, SpvOpPhi, t_u32(l), result, id, read, c_u32(l, 0), start);
    info(l, result)->type = t_u32(l);
    return result;
}

/* the switch's cases go in order between switch_begin and switch_end, each closed with case_end. */
static void switch_begin(Fn *f, Switch *sw)
{
    memset(sw, 0, sizeof(*sw));
    sw->merge = fresh(f->l);
}

static void case_begin(Fn *f, Switch *sw, uint32_t value)
{
    if (sw->count == MIME_ARRAY_COUNT(sw->values)) {
        f->l->failed = true;
        return;
    }
    sw->values[sw->count] = value;
    sw->labels[sw->count] = fresh(f->l);
    label(f, sw->labels[sw->count++]);
}

static void case_end(Fn *f, Switch *sw) { S(f, SpvOpBranch, sw->merge); }

/* the cases are written into a buffer of their own, so the OpSwitch comes first. */
static void switch_end(Fn *f, Switch *sw, uint32_t selector, Buf *cases)
{
    uint32_t fallback = fresh(f->l);
    S(f, SpvOpSelectionMerge, sw->merge, SpvSelectionControlMaskNone);
    put(&f->body, (3 + 2 * sw->count) << 16 | SpvOpSwitch);
    put(&f->body, selector);
    put(&f->body, fallback);
    for (uint32_t i = 0; i < sw->count; i++) {
        put(&f->body, sw->values[i]);
        put(&f->body, sw->labels[i]);
    }
    put_all(&f->body, cases->w, cases->n);
    label(f, fallback);
    S(f, SpvOpBranch, sw->merge);
    label(f, sw->merge);
    free(cases->w);
    memset(cases, 0, sizeof(*cases));
}

/* the body written into cases for a while, as switches need. */
static Buf divert(Fn *f, Buf *cases)
{
    Buf body = f->body;
    f->body = *cases;
    return body;
}

static void restore(Fn *f, Buf *cases, Buf body)
{
    *cases = f->body;
    f->body = body;
}

/* whether OpCopyLogical takes one type to the other: the same, or aggregates of members that are. */
static bool compatible(Lnk *l, uint32_t a, uint32_t b)
{
    if (a == b)
        return true;
    Info *x = info(l, a), *y = info(l, b);
    if (x->op != y->op || !x->at || !y->at || (x->op != SpvOpTypeStruct && x->op != SpvOpTypeArray))
        return false;
    const uint32_t *wa = l->types.w + x->at - 1, *wb = l->types.w + y->at - 1;
    uint32_t n = wa[0] >> 16;
    if (n != wb[0] >> 16)
        return false;
    if (x->op == SpvOpTypeArray)
        return wa[3] == wb[3] && compatible(l, wa[2], wb[2]);
    for (uint32_t i = 2; i < n; i++)
        if (!compatible(l, wa[i], wb[i]))
            return false;
    return true;
}

/* whether a stage's incoming payload or data can be the trace's: vulkan leaves the others undefined. */
static bool takes(Lnk *l, uint32_t var, uint32_t type)
{
    return !var || compatible(l, pointee(l, type_of_value(l, var)), type);
}

/* a copy between private variables of types that match, if not the same. */
static void copy_value(Fn *f, uint32_t to, uint32_t from)
{
    Lnk *l = f->l;
    if (!to || !from)
        return;
    uint32_t tt = pointee(l, type_of_value(l, to)), ft = pointee(l, type_of_value(l, from));
    if (!compatible(l, tt, ft))
        return;
    uint32_t value = load(f, from);
    if (tt != ft)
        value = V(f, SpvOpCopyLogical, tt, value);
    store(f, to, value);
}

/* two floats into a hit attribute variable: a vec2, a struct starting with one, or an array of two floats. */
static void store_attributes(Fn *f, uint32_t var, uint32_t bary)
{
    Lnk *l = f->l;
    if (!var)
        return;
    uint32_t type = pointee(l, type_of_value(l, var));
    uint32_t v2 = t_vec(l, t_f32(l), 2);
    if (type == v2) {
        store(f, var, bary);
        return;
    }
    Info *t = info(l, type);
    if (t->op == SpvOpTypeStruct && t->a == v2) {
        uint32_t ptr = V(f, SpvOpAccessChain, t_ptr(l, SpvStorageClassPrivate, v2), var, c_u32(l, 0));
        store(f, ptr, bary);
        return;
    }
    uint32_t elem = t->op == SpvOpTypeStruct || t->op == SpvOpTypeArray ? t->a : 0;
    if (elem == t_f32(l))
        for (uint32_t i = 0; i < 2; i++) {
            uint32_t ptr = V(f, SpvOpAccessChain, t_ptr(l, SpvStorageClassPrivate, elem), var, c_u32(l, i));
            store(f, ptr, V(f, SpvOpCompositeExtract, elem, bary, i));
        }
}

/* what a stage's built-ins read: the ray's, and the intersection's candidate or committed one. */
typedef struct Hit {
    Level *v;
    uint32_t query;     /* the ray query's variable, which ray query operations take. */
    uint32_t committed; /* 0 for the candidate, 1 for the committed intersection, ~0 for none. */
    uint32_t t;         /* the intersection's distance when it is not the query's. */
    uint32_t kind;      /* a procedural hit's kind; 0 for a triangle's. */
    uint32_t flags, cull, tmax;
} Hit;

static void set_builtins(Fn *f, const Copy *c, const Hit *h)
{
    Lnk *l = f->l;
    uint32_t q = h->v ? h->v->query : 0, f32 = t_f32(l), v3 = t_vec(l, f32, 3), u32 = t_u32(l);
    bool intersection = h->committed != ~0u;
    uint32_t which = intersection ? c_u32(l, h->committed) : 0;
    for (int b = 0; b < BI_COUNT; b++) {
        uint32_t var = c->builtin[b];
        if (!var)
            continue;
        uint32_t type = pointee(l, type_of_value(l, var)), value = 0;
        switch (b) {
        case BI_LAUNCH_ID: value = as_type(f, load(f, l->launch_id), type); break;
        case BI_LAUNCH_SIZE: value = as_type(f, load(f, l->launch_size), type); break;
        case BI_WORLD_ORIGIN:
            if (q)
                value = V(f, SpvOpRayQueryGetWorldRayOriginKHR, v3, q);
            break;
        case BI_WORLD_DIRECTION:
            if (q)
                value = V(f, SpvOpRayQueryGetWorldRayDirectionKHR, v3, q);
            break;
        case BI_OBJECT_ORIGIN:
            value = intersection ? V(f, SpvOpRayQueryGetIntersectionObjectRayOriginKHR, v3, q, which)
                                 : (q ? V(f, SpvOpRayQueryGetWorldRayOriginKHR, v3, q) : 0);
            break;
        case BI_OBJECT_DIRECTION:
            value = intersection ? V(f, SpvOpRayQueryGetIntersectionObjectRayDirectionKHR, v3, q, which)
                                 : (q ? V(f, SpvOpRayQueryGetWorldRayDirectionKHR, v3, q) : 0);
            break;
        case BI_TMIN:
            if (q)
                value = V(f, SpvOpRayQueryGetRayTMinKHR, f32, q);
            break;
        case BI_TMAX:
            if (h->t)
                value = h->t;
            else if (intersection)
                value = V(f, SpvOpRayQueryGetIntersectionTKHR, f32, q, which);
            else
                value = h->tmax;
            break;
        case BI_CUSTOM_INDEX:
            if (intersection)
                value = V(f, SpvOpRayQueryGetIntersectionInstanceCustomIndexKHR, type, q, which);
            break;
        case BI_INSTANCE_ID:
            if (intersection)
                value = V(f, SpvOpRayQueryGetIntersectionInstanceIdKHR, type, q, which);
            break;
        case BI_PRIMITIVE_ID:
            if (intersection)
                value = V(f, SpvOpRayQueryGetIntersectionPrimitiveIndexKHR, type, q, which);
            break;
        case BI_GEOMETRY_INDEX:
            if (intersection)
                value = V(f, SpvOpRayQueryGetIntersectionGeometryIndexKHR, type, q, which);
            break;
        case BI_HIT_KIND:
            if (h->kind) {
                value = as_type(f, h->kind, type);
            } else if (intersection) {
                uint32_t front = V(f, SpvOpRayQueryGetIntersectionFrontFaceKHR, t_bool(l), q, which);
                value = as_type(f, V(f, SpvOpSelect, u32, front, c_u32(l, 0xfe), c_u32(l, 0xff)), type);
            }
            break;
        case BI_OBJECT_TO_WORLD:
            if (intersection)
                value = V(f, SpvOpRayQueryGetIntersectionObjectToWorldKHR, type, q, which);
            break;
        case BI_WORLD_TO_OBJECT:
            if (intersection)
                value = V(f, SpvOpRayQueryGetIntersectionWorldToObjectKHR, type, q, which);
            break;
        case BI_RAY_FLAGS:
            if (h->flags)
                value = as_type(f, h->flags, type);
            break;
        case BI_CULL_MASK:
            if (h->cull)
                value = as_type(f, h->cull, type);
            break;
        }
        if (value)
            store(f, var, value);
    }
}

/* a stage's record pointer, the 32 bytes of the handle past the record's start. */
static void set_record(Fn *f, const Copy *c, uint32_t record)
{
    Lnk *l = f->l;
    if (!c->record)
        return;
    uint32_t data = add64(f, record, c_u64(l, VKM_RT_HANDLE_SIZE));
    store(f, c->record, V(f, SpvOpConvertUToPtr, c->record_type, data));
}

static uint32_t call0(Fn *f, uint32_t fn)
{
    return V(f, SpvOpFunctionCall, t_void(f->l), fn);
}

/* ------------------------------------------------------------------ traces. */

static const VkmRtGroup *group(Lnk *l, uint32_t i) { return &l->groups[i]; }

static bool has(uint32_t stage) { return stage != VK_SHADER_UNUSED_KHR; }

/* the gpu address of the shader binding table offsets of the instances of the structure, from the table. */
static uint32_t offsets_of(Fn *f, uint32_t structure)
{
    Lnk *l = f->l;
    uint32_t u64 = t_u64(l), u32 = t_u32(l), b = t_bool(l);
    uint64_t table_address;
    uint32_t mask;
    vkm_as_table(&table_address, &mask);
    uint32_t address = V(f, SpvOpBitcast, u64, structure);
    uint32_t hash = V(f, SpvOpUConvert, u32,
                      V(f, SpvOpShiftRightLogical, u64,
                        V(f, SpvOpIMul, u64, V(f, SpvOpShiftRightLogical, u64, address, c_u64(l, 8)),
                          c_u64(l, 0x9e3779b97f4a7c15ull)),
                        c_u64(l, 32)));
    uint32_t at_var = fresh(l), out_var = fresh(l), count_var = fresh(l);
    uint32_t pu32 = t_ptr(l, SpvStorageClassFunction, u32), pu64 = t_ptr(l, SpvStorageClassFunction, u64);
    E(&f->vars, SpvOpVariable, pu32, at_var, SpvStorageClassFunction);
    E(&f->vars, SpvOpVariable, pu64, out_var, SpvStorageClassFunction);
    E(&f->vars, SpvOpVariable, pu32, count_var, SpvStorageClassFunction);
    info(l, at_var)->type = pu32;
    info(l, out_var)->type = pu64;
    info(l, count_var)->type = pu32;
    store(f, at_var, V(f, SpvOpBitwiseAnd, u32, hash, c_u32(l, mask)));
    store(f, out_var, c_u64(l, 0));
    store(f, count_var, c_u32(l, 0));
    uint32_t header = fresh(l), check = fresh(l), body = fresh(l), cont = fresh(l), merge = fresh(l);
    S(f, SpvOpBranch, header);
    label(f, header);
    S(f, SpvOpLoopMerge, merge, cont, SpvLoopControlMaskNone);
    S(f, SpvOpBranch, check);
    label(f, check);
    uint32_t n = load(f, count_var);
    uint32_t more = V(f, SpvOpLogicalAnd, b, V(f, SpvOpULessThanEqual, b, n, c_u32(l, mask)),
                      V(f, SpvOpINotEqual, b, address, c_u64(l, 0)));
    S(f, SpvOpBranchConditional, more, body, merge);
    label(f, body);
    uint32_t at = load(f, at_var);
    uint32_t entry = add64(f, c_u64(l, table_address), V(f, SpvOpIMul, u64, widen(f, at), c_u64(l, 32)));
    uint32_t key = load_address(f, entry, u64);
    uint32_t found = V(f, SpvOpIEqual, b, key, address), empty = V(f, SpvOpIEqual, b, key, c_u64(l, 0));
    uint32_t then = if_begin(f, found);
    store(f, out_var, load_address(f, add64(f, entry, c_u64(l, 16)), u64));
    S(f, SpvOpBranch, merge);
    label(f, fresh(l));
    if_end(f, then);
    uint32_t stop = if_begin(f, empty);
    S(f, SpvOpBranch, merge);
    label(f, fresh(l));
    if_end(f, stop);
    store(f, at_var, V(f, SpvOpBitwiseAnd, u32, V(f, SpvOpIAdd, u32, at, c_u32(l, 1)), c_u32(l, mask)));
    store(f, count_var, V(f, SpvOpIAdd, u32, n, c_u32(l, 1)));
    S(f, SpvOpBranch, cont);
    label(f, cont);
    S(f, SpvOpBranch, header);
    label(f, merge);
    return load(f, out_var);
}

/*
 * the hit group record of an intersection: the instance's offset, the trace's and the geometry's index times the
 * trace's stride make its index in the hit table.
 */
static uint32_t hit_record(Fn *f, uint32_t q, uint32_t which, uint32_t offsets, uint32_t sbt_offset,
                           uint32_t sbt_stride)
{
    Lnk *l = f->l;
    uint32_t u32 = t_u32(l), b = t_bool(l);
    uint32_t inst = V(f, SpvOpRayQueryGetIntersectionInstanceIdKHR, u32, q, which);
    uint32_t geom = V(f, SpvOpRayQueryGetIntersectionGeometryIndexKHR, u32, q, which);
    /* the instance's offset, 0 when the structure has no table of them. */
    uint32_t have = V(f, SpvOpINotEqual, b, offsets, c_u64(l, 0));
    uint32_t start = fresh(l), read = fresh(l), merge = fresh(l), result = fresh(l);
    S(f, SpvOpBranch, start);
    label(f, start);
    S(f, SpvOpSelectionMerge, merge, SpvSelectionControlMaskNone);
    S(f, SpvOpBranchConditional, have, read, merge);
    label(f, read);
    uint32_t value = load_address(f, record_at(f, offsets, inst, c_u32(l, 4)), u32);
    S(f, SpvOpBranch, merge);
    label(f, merge);
    S(f, SpvOpPhi, u32, result, value, read, c_u32(l, 0), start);
    info(l, result)->type = u32;
    uint32_t index =
        V(f, SpvOpIAdd, u32, V(f, SpvOpIAdd, u32, result, sbt_offset), V(f, SpvOpIMul, u32, geom, sbt_stride));
    return record_at(f, load(f, l->sbt[2]), index, load(f, l->sbt[5]));
}

static void make_trace(Lnk *l, const Gen *g)
{
    uint32_t level = g->level, type = g->type;
    uint32_t u32 = t_u32(l), f32 = t_f32(l), v3 = t_vec(l, f32, 3), b = t_bool(l);
    uint32_t as = type_of(l, SpvOpTypeAccelerationStructureKHR, nullptr, 0);
    uint32_t params[11] = {as, u32, u32, u32, u32, u32, v3, f32, v3, f32, t_ptr(l, SpvStorageClassPrivate, type)};
    uint32_t p[11];
    Fn fn, *f = &fn;
    fn_begin(f, l, g->fn, t_void(l), params, p, 11);
    if (level >= l->depth) {
        /* past the pipeline's depth, which vulkan leaves undefined: the ray finds nothing and runs nothing. */
        S(f, SpvOpReturn);
        fn_end(f);
        return;
    }
    Level *v = level_of(l, level);
    uint32_t payload = payload_var(l, level, type);
    copy_value(f, payload, p[10]);
    store(f, v->flags, p[1]);
    store(f, v->cull, p[2]);
    store(f, v->tmax, p[9]);
    store(f, v->best, p[9]);
    store(f, v->terminate, c_bool(l, false));
    uint32_t walk = p[1];
    if (!l->any_hit) {
        /*
         * without any-hit shaders a triangle that is not opaque is accepted as an opaque one is, so every one is
         * taken as opaque, unless the ray culls by opacity: metal then walks without stopping at each candidate.
         */
        uint32_t culls = V(f, SpvOpBitwiseAnd, u32, p[1], c_u32(l, 0xc0));
        uint32_t opaque = V(f, SpvOpBitwiseOr, u32, V(f, SpvOpBitwiseAnd, u32, p[1], c_u32(l, ~2u)), c_u32(l, 1));
        walk = V(f, SpvOpSelect, u32, V(f, SpvOpIEqual, b, culls, c_u32(l, 0)), opaque, p[1]);
    }
    S(f, SpvOpRayQueryInitializeKHR, v->query, p[0], walk, p[2], p[6], p[7], p[8], p[9]);
    uint32_t offsets = offsets_of(f, p[0]);
    Hit candidate = {v, v->query, 0, 0, 0, p[1], p[2], p[9]};
    uint32_t terminate_first = V(f, SpvOpINotEqual, b, V(f, SpvOpBitwiseAnd, u32, p[1], c_u32(l, 4)), c_u32(l, 0));

    /* the walk. */
    uint32_t header = fresh(l), check = fresh(l), body = fresh(l), cont = fresh(l), merge = fresh(l);
    S(f, SpvOpBranch, header);
    label(f, header);
    S(f, SpvOpLoopMerge, merge, cont, SpvLoopControlMaskNone);
    S(f, SpvOpBranch, check);
    label(f, check);
    uint32_t more = V(f, SpvOpRayQueryProceedKHR, b, v->query);
    S(f, SpvOpBranchConditional, more, body, merge);
    label(f, body);
    uint32_t kind = V(f, SpvOpRayQueryGetIntersectionTypeKHR, u32, v->query, c_u32(l, 0));
    uint32_t record = hit_record(f, v->query, c_u32(l, 0), offsets, p[3], p[4]);
    uint32_t gid = group_at(f, record);
    uint32_t triangle = V(f, SpvOpIEqual, b, kind, c_u32(l, 0));
    uint32_t tri_label = fresh(l), box_label = fresh(l), joined = fresh(l);
    S(f, SpvOpSelectionMerge, joined, SpvSelectionControlMaskNone);
    S(f, SpvOpBranchConditional, triangle, tri_label, box_label);

    /* a triangle that is not opaque: its group's any-hit shader decides. */
    label(f, tri_label);
    store(f, l->status, c_u32(l, VKM_RT_ACCEPT));
    {
        Switch sw;
        Buf cases = {};
        switch_begin(f, &sw);
        for (uint32_t i = 0; i < l->group_count; i++) {
            const VkmRtGroup *gr = group(l, i);
            if (gr->type != VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_KHR || !has(gr->any))
                continue;
            Copy *a = copy_of(l, gr->any, level + 1, 0);
            if (!a || !takes(l, a->in, type))
                continue;
            Buf saved = divert(f, &cases);
            case_begin(f, &sw, gr->id);
            set_builtins(f, a, &candidate);
            store_attributes(f, a->attr, V(f, SpvOpRayQueryGetIntersectionBarycentricsKHR, t_vec(l, f32, 2), v->query,
                                         c_u32(l, 0)));
            set_record(f, a, record);
            copy_value(f, a->in, payload);
            call0(f, a->fn);
            copy_value(f, payload, a->in);
            case_end(f, &sw);
            restore(f, &cases, saved);
        }
        switch_end(f, &sw, gid, &cases);
    }
    uint32_t status = load(f, l->status);
    uint32_t accept = if_begin(f, V(f, SpvOpINotEqual, b, status, c_u32(l, VKM_RT_IGNORE)));
    S(f, SpvOpRayQueryConfirmIntersectionKHR, v->query);
    uint32_t stop = V(f, SpvOpLogicalOr, b, V(f, SpvOpIEqual, b, status, c_u32(l, VKM_RT_TERMINATE)), terminate_first);
    uint32_t end = if_begin(f, stop);
    S(f, SpvOpRayQueryTerminateKHR, v->query);
    if_end(f, end);
    if_end(f, accept);
    S(f, SpvOpBranch, joined);

    /* a box: its group's intersection shader reports what it hits. */
    label(f, box_label);
    {
        Switch sw;
        Buf cases = {};
        switch_begin(f, &sw);
        for (uint32_t i = 0; i < l->group_count; i++) {
            const VkmRtGroup *gr = group(l, i);
            if (gr->type != VK_RAY_TRACING_SHADER_GROUP_TYPE_PROCEDURAL_HIT_GROUP_KHR || !has(gr->intersection))
                continue;
            Copy *x = copy_of(l, gr->intersection, level, type);
            if (!x)
                continue;
            Buf saved = divert(f, &cases);
            case_begin(f, &sw, gr->id);
            store(f, v->group, c_u32(l, gr->id));
            set_builtins(f, x, &candidate);
            set_record(f, x, record);
            call0(f, x->fn);
            case_end(f, &sw);
            restore(f, &cases, saved);
        }
        switch_end(f, &sw, gid, &cases);
    }
    uint32_t done = if_begin(f, load(f, v->terminate));
    S(f, SpvOpRayQueryTerminateKHR, v->query);
    if_end(f, done);
    S(f, SpvOpBranch, joined);
    label(f, joined);
    S(f, SpvOpBranch, cont);
    label(f, cont);
    S(f, SpvOpBranch, header);
    label(f, merge);

    /* the closest hit, or the miss. */
    uint32_t committed = V(f, SpvOpRayQueryGetIntersectionTypeKHR, u32, v->query, c_u32(l, 1));
    uint32_t hit = V(f, SpvOpINotEqual, b, committed, c_u32(l, 0));
    uint32_t hit_label = fresh(l), miss_label = fresh(l), after = fresh(l);
    S(f, SpvOpSelectionMerge, after, SpvSelectionControlMaskNone);
    S(f, SpvOpBranchConditional, hit, hit_label, miss_label);
    label(f, hit_label);
    uint32_t skip = V(f, SpvOpINotEqual, b, V(f, SpvOpBitwiseAnd, u32, p[1], c_u32(l, 8)), c_u32(l, 0));
    uint32_t run = if_begin(f, V(f, SpvOpLogicalNot, b, skip));
    {
        uint32_t crecord = hit_record(f, v->query, c_u32(l, 1), offsets, p[3], p[4]);
        uint32_t cgid = group_at(f, crecord);
        Switch sw;
        Buf cases = {};
        switch_begin(f, &sw);
        for (uint32_t i = 0; i < l->group_count; i++) {
            const VkmRtGroup *gr = group(l, i);
            if ((gr->type != VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_KHR &&
                 gr->type != VK_RAY_TRACING_SHADER_GROUP_TYPE_PROCEDURAL_HIT_GROUP_KHR) ||
                !has(gr->closest))
                continue;
            Copy *c = copy_of(l, gr->closest, level + 1, 0);
            if (!c || !takes(l, c->in, type))
                continue;
            bool procedural = gr->type == VK_RAY_TRACING_SHADER_GROUP_TYPE_PROCEDURAL_HIT_GROUP_KHR;
            Buf saved = divert(f, &cases);
            case_begin(f, &sw, gr->id);
            Hit h = {v, v->query, 1, 0, procedural ? load(f, v->kind) : 0, p[1], p[2], p[9]};
            set_builtins(f, c, &h);
            if (!procedural)
                store_attributes(f, c->attr, V(f, SpvOpRayQueryGetIntersectionBarycentricsKHR, t_vec(l, f32, 2),
                                             v->query, c_u32(l, 1)));
            set_record(f, c, crecord);
            copy_value(f, c->in, payload);
            call0(f, c->fn);
            copy_value(f, payload, c->in);
            case_end(f, &sw);
            restore(f, &cases, saved);
        }
        switch_end(f, &sw, cgid, &cases);
    }
    if_end(f, run);
    S(f, SpvOpBranch, after);
    label(f, miss_label);
    {
        uint32_t mrecord = record_at(f, load(f, l->sbt[1]), p[5], load(f, l->sbt[4]));
        uint32_t mgid = group_at(f, mrecord);
        Switch sw;
        Buf cases = {};
        switch_begin(f, &sw);
        for (uint32_t i = 0; i < l->group_count; i++) {
            const VkmRtGroup *gr = group(l, i);
            if (gr->type != VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR || !has(gr->general) ||
                l->srcs[gr->general].stage != VK_SHADER_STAGE_MISS_BIT_KHR)
                continue;
            Copy *c = copy_of(l, gr->general, level + 1, 0);
            if (!c || !takes(l, c->in, type))
                continue;
            Buf saved = divert(f, &cases);
            case_begin(f, &sw, gr->id);
            Hit h = {v, v->query, ~0u, 0, 0, p[1], p[2], p[9]};
            set_builtins(f, c, &h);
            set_record(f, c, mrecord);
            copy_value(f, c->in, payload);
            call0(f, c->fn);
            copy_value(f, payload, c->in);
            case_end(f, &sw);
            restore(f, &cases, saved);
        }
        switch_end(f, &sw, mgid, &cases);
    }
    S(f, SpvOpBranch, after);
    label(f, after);
    copy_value(f, p[10], payload);
    S(f, SpvOpReturn);
    fn_end(f);
}

/* an intersection shader's report: whether the hit is kept, after its group's any-hit shader. */
static void make_report(Lnk *l, const Gen *g)
{
    uint32_t level = g->level, type = g->type;
    uint32_t u32 = t_u32(l), f32 = t_f32(l), b = t_bool(l);
    uint32_t params[2] = {f32, u32}, p[2];
    Fn fn, *f = &fn;
    fn_begin(f, l, g->fn, b, params, p, 2);
    Level *v = level_of(l, level);
    uint32_t payload = payload_var(l, level, type);
    uint32_t keep = fresh(l);
    uint32_t pb = t_ptr(l, SpvStorageClassFunction, b);
    E(&f->vars, SpvOpVariable, pb, keep, SpvStorageClassFunction);
    info(l, keep)->type = pb;
    store(f, keep, c_bool(l, false));
    uint32_t tmin = V(f, SpvOpRayQueryGetRayTMinKHR, f32, v->query);
    uint32_t inside = V(f, SpvOpLogicalAnd, b, V(f, SpvOpFOrdGreaterThanEqual, b, p[0], tmin),
                        V(f, SpvOpFOrdLessThanEqual, b, p[0], load(f, v->best)));
    uint32_t opaque = V(f, SpvOpRayQueryGetIntersectionCandidateAABBOpaqueKHR, b, v->query);
    uint32_t flags = load(f, v->flags);
    uint32_t in_range = if_begin(f, inside);
    store(f, l->status, c_u32(l, VKM_RT_ACCEPT));
    {
        Switch sw;
        Buf cases = {};
        switch_begin(f, &sw);
        for (uint32_t i = 0; i < l->group_count; i++) {
            const VkmRtGroup *gr = group(l, i);
            if (gr->type != VK_RAY_TRACING_SHADER_GROUP_TYPE_PROCEDURAL_HIT_GROUP_KHR || !has(gr->intersection))
                continue;
            Copy *x = copy_of(l, gr->intersection, level, type);
            Copy *a = has(gr->any) ? copy_of(l, gr->any, level + 1, 0) : nullptr;
            Buf saved = divert(f, &cases);
            case_begin(f, &sw, gr->id);
            if (a && x) {
                uint32_t run = if_begin(f, V(f, SpvOpLogicalNot, b, opaque));
                Hit h = {v, v->query, 0, p[0], p[1], flags, load(f, v->cull), load(f, v->tmax)};
                set_builtins(f, a, &h);
                copy_value(f, a->attr, x->attr);
                copy_value(f, a->in, payload);
                call0(f, a->fn);
                copy_value(f, payload, a->in);
                if_end(f, run);
            }
            uint32_t ok = if_begin(f, V(f, SpvOpINotEqual, b, load(f, l->status), c_u32(l, VKM_RT_IGNORE)));
            S(f, SpvOpRayQueryGenerateIntersectionKHR, v->query, p[0]);
            store(f, v->best, p[0]);
            store(f, v->kind, p[1]);
            store(f, keep, c_bool(l, true));
            if (x && has(gr->closest)) {
                Copy *c = copy_of(l, gr->closest, level + 1, 0);
                if (c)
                    copy_value(f, c->attr, x->attr);
            }
            uint32_t first = V(f, SpvOpINotEqual, b, V(f, SpvOpBitwiseAnd, u32, flags, c_u32(l, 4)), c_u32(l, 0));
            uint32_t stop = V(f, SpvOpLogicalOr, b,
                              V(f, SpvOpIEqual, b, load(f, l->status), c_u32(l, VKM_RT_TERMINATE)), first);
            uint32_t end = if_begin(f, stop);
            store(f, v->terminate, c_bool(l, true));
            if_end(f, end);
            if_end(f, ok);
            case_end(f, &sw);
            restore(f, &cases, saved);
        }
        switch_end(f, &sw, load(f, v->group), &cases);
    }
    if_end(f, in_range);
    S(f, SpvOpReturnValue, load(f, keep));
    fn_end(f);
}

static void make_callable(Lnk *l, const Gen *g)
{
    uint32_t level = g->level, type = g->type;
    uint32_t u32 = t_u32(l);
    uint32_t params[2] = {u32, t_ptr(l, SpvStorageClassPrivate, type)}, p[2];
    Fn fn, *f = &fn;
    fn_begin(f, l, g->fn, t_void(l), params, p, 2);
    if (level / CALLABLE_LEVEL > VKM_RT_CALLABLE_DEPTH) {
        /* deeper than callable shaders are unrolled. */
        S(f, SpvOpReturn);
        fn_end(f);
        return;
    }
    uint32_t record = record_at(f, load(f, l->sbt[3]), p[0], load(f, l->sbt[6]));
    uint32_t gid = group_at(f, record);
    Switch sw;
    Buf cases = {};
    switch_begin(f, &sw);
    for (uint32_t i = 0; i < l->group_count; i++) {
        const VkmRtGroup *gr = group(l, i);
        if (gr->type != VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR || !has(gr->general) ||
            l->srcs[gr->general].stage != VK_SHADER_STAGE_CALLABLE_BIT_KHR)
            continue;
        Copy *c = copy_of(l, gr->general, level, 0);
        if (!c || !takes(l, c->in, type))
            continue;
        Buf saved = divert(f, &cases);
        case_begin(f, &sw, gr->id);
        Hit h = {nullptr, 0, ~0u};
        set_builtins(f, c, &h);
        set_record(f, c, record);
        copy_value(f, c->in, p[1]);
        call0(f, c->fn);
        copy_value(f, p[1], c->in);
        case_end(f, &sw);
        restore(f, &cases, saved);
    }
    switch_end(f, &sw, gid, &cases);
    S(f, SpvOpReturn);
    fn_end(f);
}

/* the kernel: its parameters, then the ray generation group of the record vkCmdTraceRays gives. */
static uint32_t make_main(Lnk *l)
{
    uint32_t u32 = t_u32(l), u64 = t_u64(l), b = t_bool(l), uv3 = t_vec(l, u32, 3);
    uint32_t id = fresh(l);
    Fn fn, *f = &fn;
    fn_begin(f, l, id, t_void(l), nullptr, nullptr, 0);
    uint32_t values[P_COUNT];
    for (uint32_t i = 0; i < P_COUNT; i++) {
        uint32_t ptr = V(f, SpvOpAccessChain, t_ptr(l, SpvStorageClassStorageBuffer, param_type(l, i)), l->params,
                         c_u32(l, i));
        values[i] = load(f, ptr);
    }
    /* indirect traces read their sizes, and the second kind the tables too, from vulkan's commands. */
    uint32_t mode = values[P_MODE], indirect = values[P_INDIRECT];
    uint32_t sizes[3], tables[7];
    uint32_t direct = V(f, SpvOpIEqual, b, mode, c_u32(l, 0)), second = V(f, SpvOpIEqual, b, mode, c_u32(l, 2));
    for (uint32_t i = 0; i < 3; i++) {
        /* VkTraceRaysIndirectCommand2KHR has eleven 64-bit words of tables and sizes before the extent. */
        uint32_t at = V(f, SpvOpSelect, u64, second, c_u64(l, 88 + 4 * i), c_u64(l, 4 * i));
        uint32_t read = fresh(l), start = fresh(l), merge = fresh(l), phi = fresh(l);
        S(f, SpvOpBranch, start);
        label(f, start);
        S(f, SpvOpSelectionMerge, merge, SpvSelectionControlMaskNone);
        S(f, SpvOpBranchConditional, direct, merge, read);
        label(f, read);
        uint32_t value = load_address(f, add64(f, indirect, at), u32);
        S(f, SpvOpBranch, merge);
        label(f, merge);
        S(f, SpvOpPhi, u32, phi, values[P_WIDTH + i], start, value, read);
        info(l, phi)->type = u32;
        sizes[i] = phi;
    }
    static const uint32_t from[7] = {P_RAYGEN,      P_MISS,       P_HIT,           P_CALLABLE,
                                     P_MISS_STRIDE, P_HIT_STRIDE, P_CALLABLE_STRIDE};
    static const uint32_t at2[7] = {0, 16, 40, 64, 32, 56, 80};
    for (uint32_t i = 0; i < 7; i++) {
        uint32_t type = i < 4 ? u64 : u32;
        uint32_t read = fresh(l), start = fresh(l), merge = fresh(l), phi = fresh(l);
        S(f, SpvOpBranch, start);
        label(f, start);
        S(f, SpvOpSelectionMerge, merge, SpvSelectionControlMaskNone);
        S(f, SpvOpBranchConditional, second, read, merge);
        label(f, read);
        /* the strides are 64-bit in the command: their low half. */
        uint32_t value = load_address(f, add64(f, indirect, c_u64(l, at2[i])), type);
        S(f, SpvOpBranch, merge);
        label(f, merge);
        S(f, SpvOpPhi, type, phi, value, read, values[from[i]], start);
        info(l, phi)->type = type;
        tables[i] = phi;
        store(f, l->sbt[i], tables[i]);
    }
    uint32_t size = V(f, SpvOpCompositeConstruct, uv3, sizes[0], sizes[1], sizes[2]);
    uint32_t gid = load(f, l->global_id);
    uint32_t inside = V(f, SpvOpAll, b, V(f, SpvOpULessThan, t_vec(l, b, 3), gid, size));
    uint32_t in = if_begin(f, inside);
    store(f, l->launch_id, gid);
    store(f, l->launch_size, size);
    uint32_t record = load(f, l->sbt[0]);
    uint32_t rgid = group_at(f, record);
    Switch sw;
    Buf cases = {};
    switch_begin(f, &sw);
    for (uint32_t i = 0; i < l->group_count; i++) {
        const VkmRtGroup *gr = group(l, i);
        if (gr->type != VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR || !has(gr->general) ||
            l->srcs[gr->general].stage != VK_SHADER_STAGE_RAYGEN_BIT_KHR)
            continue;
        Copy *c = copy_of(l, gr->general, 0, 0);
        if (!c)
            continue;
        Buf saved = divert(f, &cases);
        case_begin(f, &sw, gr->id);
        Hit h = {nullptr, 0, ~0u};
        set_builtins(f, c, &h);
        set_record(f, c, record);
        call0(f, c->fn);
        case_end(f, &sw);
        restore(f, &cases, saved);
    }
    switch_end(f, &sw, rgid, &cases);
    if_end(f, in);
    S(f, SpvOpReturn);
    fn_end(f);
    return id;
}

/* ------------------------------------------------------------------ the module. */

bool vkm_rt_link(const VkmRtStage *stages, uint32_t stage_count, const VkmRtGroup *groups, uint32_t group_count,
                 uint32_t depth, uint32_t **out, size_t *out_words)
{
    Lnk lnk = {}, *l = &lnk;
    l->bound = 1;
    l->stages = stages;
    l->stage_count = stage_count;
    l->groups = groups;
    l->group_count = group_count;
    l->depth = MIME_MIN(MIME_MAX(depth, 1u), (uint32_t)VKM_RT_MAX_DEPTH);
    for (uint32_t i = 0; i < group_count; i++)
        l->any_hit |= groups[i].type != VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR && has(groups[i].any);
    l->srcs = vkm_zalloc(sizeof(Src) * MIME_MAX(stage_count, 1u));
    bool ok = l->srcs != nullptr;
    uint32_t version = 0x10400;
    bool vulkan_model = false;
    for (uint32_t i = 0; ok && i < stage_count; i++) {
        ok = parse(&l->srcs[i], &stages[i]);
        if (!ok)
            VKM_ERROR("ray tracing: stage %u of the pipeline does not parse", i);
        else {
            version = MIME_MAX(version, l->srcs[i].version);
            vulkan_model |= l->srcs[i].vulkan_memory_model;
        }
    }
    uint32_t main_fn = 0;
    if (ok) {
        l->glsl = fresh(l);
        capability(l, SpvCapabilityShader);
        capability(l, SpvCapabilityInt64);
        capability(l, SpvCapabilityRayQueryKHR);
        capability(l, SpvCapabilityPhysicalStorageBufferAddresses);
        if (vulkan_model)
            capability(l, SpvCapabilityVulkanMemoryModel);
        for (uint32_t i = 0; i < stage_count; i++)
            for (size_t at = 5; at < l->srcs[i].n;) {
                const uint32_t *w = l->srcs[i].w + at;
                if ((w[0] & 0xffff) == SpvOpCapability && w[1] != SpvCapabilityRayTracingKHR &&
                    w[1] != SpvCapabilityRayTracingNV && w[1] != SpvCapabilityRayTracingProvisionalKHR)
                    capability(l, w[1]);
                if ((w[0] & 0xffff) == SpvOpFunction)
                    break;
                at += w[0] >> 16;
            }
        params_init(l);
        main_fn = make_main(l);
        /* each function made may ask for more. */
        for (uint32_t i = 0; i < l->gen_count && !l->failed && !l->oom; i++) {
            Gen g = l->gens[i];
            switch (g.kind) {
            case GEN_TRACE: make_trace(l, &g); break;
            case GEN_REPORT: make_report(l, &g); break;
            case GEN_CALLABLE: make_callable(l, &g); break;
            }
        }
        ok = !l->failed && !l->oom;
    }
    if (ok) {
        Buf m = {};
        put(&m, SpvMagicNumber);
        put(&m, version);
        put(&m, 0);
        put(&m, l->bound);
        put(&m, 0);
        for (uint32_t i = 0; i < l->cap_count; i++)
            E(&m, SpvOpCapability, l->caps[i]);
        static const char *const extensions[] = {"SPV_KHR_ray_query", "SPV_KHR_physical_storage_buffer",
                                                 "SPV_KHR_storage_buffer_storage_class",
                                                 "SPV_KHR_non_semantic_info"};
        for (size_t i = 0; i < MIME_ARRAY_COUNT(extensions); i++) {
            Buf s = {};
            string_words(&s, extensions[i]);
            emit(&m, SpvOpExtension, s.w, (uint32_t)s.n);
            free(s.w);
        }
        for (uint32_t i = 0; i < stage_count; i++)
            for (size_t at = 5; at < l->srcs[i].n;) {
                const uint32_t *w = l->srcs[i].w + at;
                uint32_t op = w[0] & 0xffff;
                if (op == SpvOpExtension) {
                    const char *name = (const char *)&w[1];
                    bool skip = !strcmp(name, "SPV_KHR_ray_tracing") || !strcmp(name, "SPV_NV_ray_tracing");
                    for (size_t k = 0; k < MIME_ARRAY_COUNT(extensions); k++)
                        skip |= !strcmp(name, extensions[k]);
                    if (!skip)
                        put_all(&m, w, w[0] >> 16);
                }
                if (op == SpvOpFunction)
                    break;
                at += w[0] >> 16;
            }
        {
            Buf s = {};
            put(&s, l->glsl);
            string_words(&s, "GLSL.std.450");
            emit(&m, SpvOpExtInstImport, s.w, (uint32_t)s.n);
            free(s.w);
        }
        E(&m, SpvOpMemoryModel, SpvAddressingModelPhysicalStorageBuffer64,
          vulkan_model ? SpvMemoryModelVulkan : SpvMemoryModelGLSL450);
        {
            Buf s = {};
            put(&s, SpvExecutionModelGLCompute);
            put(&s, main_fn);
            string_words(&s, "main");
            put_all(&s, l->interface.w, l->interface.n);
            emit(&m, SpvOpEntryPoint, s.w, (uint32_t)s.n);
            free(s.w);
        }
        E(&m, SpvOpExecutionMode, main_fn, SpvExecutionModeLocalSize, VKM_RT_LOCAL_X, VKM_RT_LOCAL_Y, 1);
        put_all(&m, l->decos.w, l->decos.n);
        put_all(&m, l->types.w, l->types.n);
        put_all(&m, l->funcs.w, l->funcs.n);
        m.w[3] = l->bound;
        ok = !m.oom && !l->types.oom && !l->decos.oom && !l->funcs.oom;
        if (ok) {
            *out = m.w;
            *out_words = m.n;
        } else {
            free(m.w);
        }
    }
    for (uint32_t i = 0; l->srcs && i < stage_count; i++)
        src_free(&l->srcs[i]);
    free(l->srcs);
    free(l->info);
    free(l->keys);
    free(l->slots);
    for (uint32_t i = 0; i < l->copy_count; i++)
        free(l->copies[i]);
    free(l->copies);
    free(l->gens);
    free(l->interface.w);
    free(l->types.w);
    free(l->decos.w);
    free(l->funcs.w);
    return ok;
}

/* ------------------------------------------------------------------ pipelines. */

/* what a pipeline keeps of its stages and groups, for the pipelines a library goes into. */
struct VkmRtPipeline {
    VkmRtStage *stages;
    uint32_t stage_count;
    VkmRtGroup *groups;
    uint32_t group_count;
};

/* every group's id, so a library's handles stay the same in the pipelines it goes into. */
static uint32_t g_next_group = 1;

void vkm_rt_free(VkmRtPipeline *rt)
{
    if (!rt)
        return;
    for (uint32_t i = 0; i < rt->stage_count; i++) {
        VkmRtStage *s = &rt->stages[i];
        free((void *)s->code);
        free((void *)s->entry);
        if (s->spec) {
            free((void *)s->spec->pMapEntries);
            free((void *)s->spec->pData);
            free((void *)s->spec);
        }
    }
    free(rt->stages);
    free(rt->groups);
    free(rt);
}

static bool copy_stage(VkmRtStage *out, const uint32_t *code, size_t words, const char *entry,
                       VkShaderStageFlagBits stage, const VkSpecializationInfo *spec)
{
    memset(out, 0, sizeof(*out));
    uint32_t *c = malloc(words * 4);
    char *e = strdup(entry ? entry : "main");
    if (!c || !e) {
        free(c);
        free(e);
        return false;
    }
    memcpy(c, code, words * 4);
    out->code = c;
    out->words = words;
    out->entry = e;
    out->stage = stage;
    if (spec && spec->mapEntryCount) {
        VkSpecializationInfo *s = malloc(sizeof(*s));
        VkSpecializationMapEntry *entries = malloc(sizeof(*entries) * spec->mapEntryCount);
        void *data = malloc(MIME_MAX(spec->dataSize, (size_t)1));
        if (!s || !entries || !data) {
            free(s);
            free(entries);
            free(data);
            return false;
        }
        memcpy(entries, spec->pMapEntries, sizeof(*entries) * spec->mapEntryCount);
        memcpy(data, spec->pData, spec->dataSize);
        *s = (VkSpecializationInfo){spec->mapEntryCount, entries, spec->dataSize, data};
        out->spec = s;
    }
    return true;
}

static VkResult gather(const VkRayTracingPipelineCreateInfoKHR *info, VkmRtPipeline *rt)
{
    const VkPipelineLibraryCreateInfoKHR *libs = info->pLibraryInfo;
    uint32_t stages = info->stageCount, groups = info->groupCount;
    for (uint32_t i = 0; libs && i < libs->libraryCount; i++) {
        VkmRtPipeline *lib = libs->pLibraries[i]->rt;
        if (!lib)
            return VK_ERROR_INITIALIZATION_FAILED;
        stages += lib->stage_count;
        groups += lib->group_count;
    }
    rt->stages = vkm_zalloc(sizeof(*rt->stages) * MIME_MAX(stages, 1u));
    rt->groups = vkm_zalloc(sizeof(*rt->groups) * MIME_MAX(groups, 1u));
    if (!rt->stages || !rt->groups)
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    for (uint32_t i = 0; i < info->stageCount; i++) {
        const VkPipelineShaderStageCreateInfo *s = &info->pStages[i];
        const VkShaderModuleCreateInfo *inline_code = vkm_find(s->pNext, VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO);
        const uint32_t *code = s->module ? s->module->code : inline_code ? inline_code->pCode : nullptr;
        size_t size = s->module ? s->module->size : inline_code ? inline_code->codeSize : 0;
        if (!code)
            return VK_ERROR_INVALID_SHADER_NV;
        if (!copy_stage(&rt->stages[rt->stage_count], code, size / 4, s->pName, s->stage, s->pSpecializationInfo))
            return VK_ERROR_OUT_OF_HOST_MEMORY;
        rt->stage_count++;
    }
    /* the pipeline's groups, then each library's in order, as vulkan numbers them. */
    for (uint32_t i = 0; i < info->groupCount; i++) {
        const VkRayTracingShaderGroupCreateInfoKHR *g = &info->pGroups[i];
        rt->groups[rt->group_count++] =
            (VkmRtGroup){__atomic_fetch_add(&g_next_group, 1, __ATOMIC_RELAXED), g->type, g->generalShader,
                         g->closestHitShader, g->anyHitShader, g->intersectionShader};
    }
    for (uint32_t i = 0; libs && i < libs->libraryCount; i++) {
        const VkmRtPipeline *lib = libs->pLibraries[i]->rt;
        uint32_t base = rt->stage_count;
        for (uint32_t k = 0; k < lib->stage_count; k++) {
            const VkmRtStage *s = &lib->stages[k];
            if (!copy_stage(&rt->stages[rt->stage_count], s->code, s->words, s->entry, s->stage, s->spec))
                return VK_ERROR_OUT_OF_HOST_MEMORY;
            rt->stage_count++;
        }
        for (uint32_t k = 0; k < lib->group_count; k++) {
            VkmRtGroup g = lib->groups[k];
            uint32_t *at[4] = {&g.general, &g.closest, &g.any, &g.intersection};
            for (int j = 0; j < 4; j++)
                if (*at[j] != VK_SHADER_UNUSED_KHR)
                    *at[j] += base;
            rt->groups[rt->group_count++] = g;
        }
    }
    return VK_SUCCESS;
}

static VKAPI_ATTR VkResult VKAPI_CALL CreateRayTracingPipelinesKHR(VkDevice device, VkDeferredOperationKHR op,
                                                                   VkPipelineCache cache, uint32_t count,
                                                                   const VkRayTracingPipelineCreateInfoKHR *infos,
                                                                   const VkAllocationCallbacks *a, VkPipeline *out)
{
    VkResult result = VK_SUCCESS;
    for (uint32_t i = 0; i < count; i++) {
        const VkRayTracingPipelineCreateInfoKHR *info = &infos[i];
        out[i] = VK_NULL_HANDLE;
        VkPipeline p = vkm_zalloc(sizeof(*p));
        VkmRtPipeline *rt = vkm_zalloc(sizeof(*rt));
        if (!p || !rt) {
            free(p);
            free(rt);
            return VK_ERROR_OUT_OF_HOST_MEMORY;
        }
        p->bind = VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR;
        p->rt = rt;
        VkResult r = gather(info, rt);
        if (r == VK_SUCCESS && !(info->flags & VK_PIPELINE_CREATE_LIBRARY_BIT_KHR)) {
            uint32_t *words = nullptr;
            size_t n = 0;
            if (!vkm_rt_link(rt->stages, rt->stage_count, rt->groups, rt->group_count,
                             info->maxPipelineRayRecursionDepth, &words, &n)) {
                VKM_ERROR("ray tracing: a pipeline of %u stages and %u groups does not link", rt->stage_count,
                          rt->group_count);
                r = VK_ERROR_INVALID_SHADER_NV;
            } else {
                r = vkm_compile_rays(device, words, n * 4, info->layout, &p->stages[0]);
                p->has[0] = r == VK_SUCCESS;
                if (r == VK_SUCCESS && !(p->compute = [vkm_compute_pipeline(device, &p->stages[0], false) retain]))
                    r = VK_ERROR_INVALID_SHADER_NV;
            }
            free(words);
        }
        if (r != VK_SUCCESS) {
            vkm_pipeline_free(p);
            result = r;
            continue;
        }
        out[i] = p;
    }
    return result;
}

static VKAPI_ATTR VkResult VKAPI_CALL GetRayTracingShaderGroupHandlesKHR(VkDevice device, VkPipeline pipeline,
                                                                         uint32_t first, uint32_t count, size_t size,
                                                                         void *data)
{
    const VkmRtPipeline *rt = pipeline->rt;
    if (!rt || first + count > rt->group_count || size < (size_t)count * VKM_RT_HANDLE_SIZE)
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    memset(data, 0, (size_t)count * VKM_RT_HANDLE_SIZE);
    for (uint32_t i = 0; i < count; i++)
        memcpy((uint8_t *)data + (size_t)i * VKM_RT_HANDLE_SIZE, &rt->groups[first + i].id, 4);
    return VK_SUCCESS;
}

static VKAPI_ATTR VkResult VKAPI_CALL GetRayTracingCaptureReplayShaderGroupHandlesKHR(VkDevice device,
                                                                                      VkPipeline pipeline,
                                                                                      uint32_t first, uint32_t count,
                                                                                      size_t size, void *data)
{
    return GetRayTracingShaderGroupHandlesKHR(device, pipeline, first, count, size, data);
}

/* what a stage's functions keep in variables of their own, the nearest thing the kernel has to a shader's stack. */
static VkDeviceSize stage_stack(const VkmRtStage *stage)
{
    const uint32_t *code = stage->code;
    uint32_t bound = stage->words > 4 ? code[3] : 0;
    /* per id: a type's bytes, a pointer's pointee or a constant's value. */
    uint32_t *bytes = vkm_zalloc(sizeof(uint32_t) * MIME_MAX(bound, 1u) * 3);
    if (!bytes)
        return 0;
    uint32_t *pointee = bytes + bound, *value = pointee + bound;
    uint64_t total = 0;
    for (size_t at = 5; at < stage->words;) {
        const uint32_t *w = code + at;
        uint32_t op = w[0] & 0xffff, count = w[0] >> 16;
        if (!count || at + count > stage->words)
            break;
        at += count;
        uint32_t id = op == SpvOpConstant || op == SpvOpVariable ? w[2] : w[1];
        if (count < 2 || id >= bound)
            continue;
        switch (op) {
        case SpvOpTypeBool: bytes[id] = 4; break;
        case SpvOpTypeInt:
        case SpvOpTypeFloat: bytes[id] = w[2] / 8; break;
        case SpvOpTypeVector:
        case SpvOpTypeMatrix: bytes[id] = w[2] < bound ? bytes[w[2]] * w[3] : 0; break;
        case SpvOpTypeArray:
            bytes[id] = w[2] < bound && w[3] < bound ? bytes[w[2]] * value[w[3]] : 0;
            break;
        case SpvOpTypeStruct:
            for (uint32_t i = 2; i < count; i++)
                bytes[id] += w[i] < bound ? bytes[w[i]] : 0;
            break;
        case SpvOpTypePointer:
            bytes[id] = 8;
            pointee[id] = w[3];
            break;
        case SpvOpTypeRayQueryKHR: bytes[id] = 256; break;
        case SpvOpConstant: value[id] = w[3]; break;
        case SpvOpVariable:
            if (w[3] == SpvStorageClassFunction && w[1] < bound && pointee[w[1]] < bound)
                total += MIME_ALIGN_UP(bytes[pointee[w[1]]], 8u);
            break;
        default: break;
        }
    }
    free(bytes);
    return MIME_ALIGN_UP(total + 32, (uint64_t)16);
}

static VKAPI_ATTR VkDeviceSize VKAPI_CALL GetRayTracingShaderGroupStackSizeKHR(VkDevice device, VkPipeline pipeline,
                                                                               uint32_t group,
                                                                               VkShaderGroupShaderKHR shader)
{
    const VkmRtPipeline *rt = pipeline->rt;
    if (!rt || group >= rt->group_count)
        return 0;
    const VkmRtGroup *g = &rt->groups[group];
    uint32_t stage = shader == VK_SHADER_GROUP_SHADER_CLOSEST_HIT_KHR ? g->closest
                     : shader == VK_SHADER_GROUP_SHADER_ANY_HIT_KHR   ? g->any
                     : shader == VK_SHADER_GROUP_SHADER_INTERSECTION_KHR ? g->intersection
                                                                         : g->general;
    return stage < rt->stage_count ? stage_stack(&rt->stages[stage]) : 0;
}

/* the sizes of an indirect trace, read from vulkan's command, as threadgroups of the kernel. */
static const char g_rays[] =
    "#include <metal_stdlib>\n"
    "using namespace metal;\n"
    "struct P { ulong indirect, out; uint mode; };\n"
    "kernel void meta_rays(constant P &p [[buffer(0)]]) {\n"
    "    device const uint *in = reinterpret_cast<device const uint *>(p.indirect + (p.mode == 2 ? 88 : 0));\n"
    "    device uint *o = reinterpret_cast<device uint *>(p.out);\n"
    "    o[0] = (in[0] + 7) / 8;\n"
    "    o[1] = (in[1] + 7) / 8;\n"
    "    o[2] = in[2];\n"
    "}\n";

id<MTLComputePipelineState> vkm_meta_rays(VkDevice device)
{
    static id<MTLComputePipelineState> state;
    return vkm_meta_kernel(device, g_rays, "meta_rays", &state);
}

const VkmEntry vkm_rt_entries[] = {
    VKM_ENTRY(CreateRayTracingPipelinesKHR),
    VKM_ENTRY(GetRayTracingShaderGroupHandlesKHR),
    VKM_ENTRY(GetRayTracingCaptureReplayShaderGroupHandlesKHR),
    VKM_ENTRY(GetRayTracingShaderGroupStackSizeKHR),
    {nullptr, nullptr},
};
