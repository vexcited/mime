/*
 * ray tracing: the stages of ray tracing pipelines, to SPV_KHR_ray_tracing,
 * and ray queries of shader model 6.5, to SPV_KHR_ray_query. a handle dxil
 * gives a query is an index in an array of them, so handles may go through
 * phis, selects and arrays as dxil lets them.
 */
#include "internal.h"

enum {
    OP_ALLOCATE = 178,
    OP_TRACE = 179,
    OP_PROCEED = 180,
    OP_ABORT = 181,
    OP_COMMIT_TRIANGLE = 182,
    OP_COMMIT_PROCEDURAL = 183,
};

static int is_allocation(Compiler *c, const DxFunction *f, const DxInst *ins)
{
    if (ins->op != DI_CALL || ins->count < 3)
        return 0;
    const DxValue *op = dx_value(&c->m, f, ins->ops[1]);
    return op && op->kind == DV_INT && op->bits == OP_ALLOCATE;
}

/* each allocation is a query of the array, numbered in the order of the function's instructions. */
void dxil_prepare_ray_queries(Compiler *c, const DxFunction *f)
{
    uint32_t n = 0;
    for (uint32_t i = 0; i < f->inst_count; i++)
        n += is_allocation(c, f, &f->insts[i]);
    if (!n)
        return;
    spv_capability(&c->b, SpvCapabilityRayQueryKHR);
    spv_extension(&c->b, "SPV_KHR_ray_query");
    uint32_t array = spv_type_array(&c->b, spv_type_opaque(&c->b, SpvOpTypeRayQueryKHR), n);
    c->ray_queries = dxil_global(c, SpvStorageClassPrivate, array, 0);
    c->ray_query_count = n;
    c->ray_query_constant_flags = dxil_alloc(c, n * sizeof(uint32_t));
    c->ray_query_insts = dxil_alloc(c, n * sizeof(uint32_t));
    if (c->error)
        return;
    for (uint32_t i = 0, k = 0; i < f->inst_count; i++)
        if (is_allocation(c, f, &f->insts[i])) {
            c->ray_query_constant_flags[k] = (uint32_t)dxil_constant(c, f->insts[i].ops[2], 0);
            c->ray_query_insts[k++] = i;
        }
}

static uint32_t query(Compiler *c, uint32_t id)
{
    uint32_t w[2] = {c->ray_queries, dxil_value(c, id)};
    return spv_op(&c->b, SpvOpAccessChain,
                  spv_type_pointer(&c->b, SpvStorageClassPrivate, spv_type_opaque(&c->b, SpvOpTypeRayQueryKHR)), w, 2);
}

/* the flags the query's allocation gives, a constant when its handle is one or all allocations agree. */
static uint32_t allocation_flags(Compiler *c, uint32_t id)
{
    uint64_t k = dxil_constant(c, id, ~0ull);
    if (k < c->ray_query_count)
        return dxil_uconst(c, c->ray_query_constant_flags[k]);
    uint32_t same = 1;
    for (uint32_t i = 1; i < c->ray_query_count; i++)
        same &= c->ray_query_constant_flags[i] == c->ray_query_constant_flags[0];
    if (same)
        return dxil_uconst(c, c->ray_query_constant_flags[0]);
    if (!c->ray_query_flags) {
        uint32_t *parts = dxil_alloc(c, c->ray_query_count * sizeof(uint32_t));
        for (uint32_t i = 0; i < c->ray_query_count; i++)
            parts[i] = dxil_uconst(c, c->ray_query_constant_flags[i]);
        uint32_t array = spv_type_array(&c->b, c->t_u32, c->ray_query_count);
        c->ray_query_flags = dxil_global(c, SpvStorageClassPrivate, array,
                                         spv_const_composite_array(&c->b, array, parts, c->ray_query_count));
    }
    uint32_t w[2] = {c->ray_query_flags, dxil_value(c, id)};
    uint32_t ptr = spv_op(&c->b, SpvOpAccessChain, spv_type_pointer(&c->b, SpvStorageClassPrivate, c->t_u32), w, 2);
    return spv_load(&c->b, c->t_u32, ptr);
}

static uint32_t vec3_of(Compiler *c, const DxInst *ins, uint32_t first)
{
    uint32_t parts[3];
    for (uint32_t i = 0; i < 3; i++)
        parts[i] = dxil_value(c, ins->ops[first + i]);
    return dxil_vector(c, c->t_f32, parts, 3);
}

static int trace(Compiler *c, const DxInst *ins)
{
    if (ins->count < 14)
        return dxil_fail(c, "a short TraceRayInline");
    uint32_t as = dxil_acceleration_structure(c, ins->ops[3]);
    if (!as)
        return -1;
    uint32_t flags = spv_op2(&c->b, SpvOpBitwiseOr, c->t_u32, dxil_value(c, ins->ops[4]), allocation_flags(c, ins->ops[2]));
    uint32_t w[8] = {query(c, ins->ops[2]), as, flags, dxil_value(c, ins->ops[5]), vec3_of(c, ins, 6),
                     dxil_value(c, ins->ops[9]), vec3_of(c, ins, 10), dxil_value(c, ins->ops[13])};
    spv_op_void(&c->b, SpvOpRayQueryInitializeKHR, w, 8);
    return 0;
}

/* what the state operations read: the spir-v op, whether it takes committed or candidate, the result's shape. */
typedef struct StateOp {
    uint16_t dx;
    uint16_t spv;
    int8_t committed;
    /* 0 a float, 1 an unsigned integer, 2 a bool, 3 a vec2, 4 a vec3, 5 a 4x3 matrix. */
    uint8_t shape;
    /* the bool result is the opposite. */
    uint8_t negate;
} StateOp;

static const StateOp STATE_OPS[] = {
    {184, SpvOpRayQueryGetIntersectionTypeKHR, 1, 1, 0},
    {185, SpvOpRayQueryGetIntersectionTypeKHR, 0, 1, 0},
    {186, SpvOpRayQueryGetIntersectionObjectToWorldKHR, 0, 5, 0},
    {187, SpvOpRayQueryGetIntersectionWorldToObjectKHR, 0, 5, 0},
    {188, SpvOpRayQueryGetIntersectionObjectToWorldKHR, 1, 5, 0},
    {189, SpvOpRayQueryGetIntersectionWorldToObjectKHR, 1, 5, 0},
    {190, SpvOpRayQueryGetIntersectionCandidateAABBOpaqueKHR, -1, 2, 1},
    {191, SpvOpRayQueryGetIntersectionFrontFaceKHR, 0, 2, 0},
    {192, SpvOpRayQueryGetIntersectionFrontFaceKHR, 1, 2, 0},
    {193, SpvOpRayQueryGetIntersectionBarycentricsKHR, 0, 3, 0},
    {194, SpvOpRayQueryGetIntersectionBarycentricsKHR, 1, 3, 0},
    {195, SpvOpRayQueryGetRayFlagsKHR, -1, 1, 0},
    {196, SpvOpRayQueryGetWorldRayOriginKHR, -1, 4, 0},
    {197, SpvOpRayQueryGetWorldRayDirectionKHR, -1, 4, 0},
    {198, SpvOpRayQueryGetRayTMinKHR, -1, 0, 0},
    {199, SpvOpRayQueryGetIntersectionTKHR, 0, 0, 0},
    {200, SpvOpRayQueryGetIntersectionTKHR, 1, 0, 0},
    {201, SpvOpRayQueryGetIntersectionInstanceIdKHR, 0, 1, 0},
    {202, SpvOpRayQueryGetIntersectionInstanceCustomIndexKHR, 0, 1, 0},
    {203, SpvOpRayQueryGetIntersectionGeometryIndexKHR, 0, 1, 0},
    {204, SpvOpRayQueryGetIntersectionPrimitiveIndexKHR, 0, 1, 0},
    {205, SpvOpRayQueryGetIntersectionObjectRayOriginKHR, 0, 4, 0},
    {206, SpvOpRayQueryGetIntersectionObjectRayDirectionKHR, 0, 4, 0},
    {207, SpvOpRayQueryGetIntersectionInstanceIdKHR, 1, 1, 0},
    {208, SpvOpRayQueryGetIntersectionInstanceCustomIndexKHR, 1, 1, 0},
    {209, SpvOpRayQueryGetIntersectionGeometryIndexKHR, 1, 1, 0},
    {210, SpvOpRayQueryGetIntersectionPrimitiveIndexKHR, 1, 1, 0},
    {211, SpvOpRayQueryGetIntersectionObjectRayOriginKHR, 1, 4, 0},
    {212, SpvOpRayQueryGetIntersectionObjectRayDirectionKHR, 1, 4, 0},
    {214, SpvOpRayQueryGetIntersectionInstanceShaderBindingTableRecordOffsetKHR, 0, 1, 0},
    {215, SpvOpRayQueryGetIntersectionInstanceShaderBindingTableRecordOffsetKHR, 1, 1, 0},
};

static int state(Compiler *c, const DxInst *ins, const StateOp *s)
{
    uint32_t type;
    switch (s->shape) {
    case 0: type = c->t_f32; break;
    case 1: type = c->t_u32; break;
    case 2: type = c->t_bool; break;
    case 3: type = c->t_vec[2]; break;
    case 4: type = c->t_vec[3]; break;
    default: type = spv_type_matrix(&c->b, c->t_vec[3], 4); break;
    }
    uint32_t w[2] = {query(c, ins->ops[2]), 0}, n = 1;
    if (s->committed >= 0)
        w[n++] = dxil_uconst(c, (uint32_t)s->committed);
    uint32_t v = spv_op(&c->b, s->spv, type, w, n);
    if (s->negate)
        v = spv_op1(&c->b, SpvOpLogicalNot, c->t_bool, v);
    if (s->shape == 3 || s->shape == 4) {
        uint64_t i = dxil_constant(c, ins->ops[3], ~0ull);
        if (i >= (s->shape == 3 ? 2u : 3u))
            return dxil_fail(c, "a ray query vector read at an index that is not constant");
        v = dxil_extract(c, c->t_f32, v, (uint32_t)i);
    } else if (s->shape == 5) {
        /* dxil reads row and column of the 3x4 matrix, spir-v has its 4 columns of 3. */
        uint64_t row = dxil_constant(c, ins->ops[3], ~0ull), col = dxil_constant(c, ins->ops[4], ~0ull);
        if (row >= 3 || col >= 4)
            return dxil_fail(c, "a ray query matrix read at an index that is not constant");
        uint32_t e[3] = {v, (uint32_t)col, (uint32_t)row};
        v = spv_op(&c->b, SpvOpCompositeExtract, c->t_f32, e, 3);
    }
    dxil_set_plain(c, ins->value, v);
    return 0;
}

/* ------------------------------------------------------------------ ray tracing stages */

int dxil_is_ray_stage(uint32_t kind) { return kind >= KIND_RAYGEN && kind <= KIND_CALLABLE; }

static uint32_t pointee_type(Compiler *c, uint32_t dx_pointer_type, uint32_t *dx_element)
{
    const DxType *t = &c->m.types[dx_pointer_type];
    if (t->kind != DT_POINTER) {
        dxil_fail(c, "a payload that is not a pointer");
        return c->t_u32;
    }
    if (dx_element)
        *dx_element = t->element;
    return dxil_type(c, t->element);
}

/* the payload of a hit or miss shader, a callable shader's data, and the attributes of a hit. */
int dxil_ray_arguments(Compiler *c, const DxFunction *f)
{
    const DxType *ft = &c->m.types[f->type];
    for (uint32_t k = 0; k < ft->count && k < 2; k++) {
        uint32_t storage;
        if (k == 1)
            storage = SpvStorageClassHitAttributeKHR;
        else if (c->kind == KIND_CALLABLE)
            storage = SpvStorageClassIncomingCallableDataKHR;
        else
            storage = SpvStorageClassIncomingRayPayloadKHR;
        uint32_t type = pointee_type(c, ft->members[k], NULL);
        uint32_t *var = k == 1 ? &c->hit_attributes : &c->incoming;
        if (!*var)
            *var = dxil_global(c, storage, type, 0);
        dxil_set_pointer(c, c->m.value_count + k, *var, type, storage);
    }
    return c->error ? -1 : 0;
}

/* the variable a trace or call passes, one by type, each at a location of its own. */
static uint32_t outgoing(Compiler *c, uint32_t type, uint32_t storage)
{
    uint32_t location = 0;
    for (uint32_t i = 0; i < c->payload_count; i++) {
        if (c->payloads[i].type == type && c->payloads[i].storage == storage)
            return c->payloads[i].var;
        location += c->payloads[i].storage == storage;
    }
    if (c->payload_count == sizeof(c->payloads) / sizeof(c->payloads[0])) {
        dxil_fail(c, "too many kinds of payloads");
        return 0;
    }
    uint32_t var = dxil_global(c, storage, type, 0);
    dxil_decorate(c, var, SpvDecorationLocation, location);
    c->payloads[c->payload_count].type = type;
    c->payloads[c->payload_count].var = var;
    c->payloads[c->payload_count++].storage = storage;
    return var;
}

/* a call passing what a pointer points to, written back after. */
static int pass_through(Compiler *c, uint32_t pointer, uint32_t storage, uint32_t op, const uint32_t *w, uint32_t n)
{
    uint32_t type;
    uint32_t value = dxil_load_pointer(c, pointer, &type);
    if (!value)
        return -1;
    uint32_t var = outgoing(c, type, storage);
    if (!var)
        return -1;
    spv_store(&c->b, var, value);
    uint32_t words[12];
    __builtin_memcpy(words, w, n * sizeof(uint32_t));
    words[n] = var;
    spv_op_void(&c->b, op, words, n + 1);
    dxil_store_pointer(c, pointer, spv_load(&c->b, type, var));
    return 0;
}

static int trace_ray(Compiler *c, const DxInst *ins)
{
    if (ins->count < 17)
        return dxil_fail(c, "a short TraceRay");
    uint32_t as = dxil_acceleration_structure(c, ins->ops[2]);
    if (!as)
        return -1;
    uint32_t w[10] = {as,
                      dxil_value(c, ins->ops[3]),
                      dxil_value(c, ins->ops[4]),
                      dxil_value(c, ins->ops[5]),
                      dxil_value(c, ins->ops[6]),
                      dxil_value(c, ins->ops[7]),
                      vec3_of(c, ins, 8),
                      dxil_value(c, ins->ops[11]),
                      vec3_of(c, ins, 12),
                      dxil_value(c, ins->ops[15])};
    return pass_through(c, ins->ops[16], SpvStorageClassRayPayloadKHR, SpvOpTraceRayKHR, w, 10);
}

static int report_hit(Compiler *c, const DxInst *ins)
{
    uint32_t type;
    uint32_t value = dxil_load_pointer(c, ins->ops[4], &type);
    if (!value)
        return -1;
    if (!c->hit_attributes)
        c->hit_attributes = dxil_global(c, SpvStorageClassHitAttributeKHR, type, 0);
    spv_store(&c->b, c->hit_attributes, value);
    dxil_set_plain(c, ins->value, spv_op2(&c->b, SpvOpReportIntersectionKHR, c->t_bool, dxil_value(c, ins->ops[2]),
                                          dxil_value(c, ins->ops[3])));
    return 0;
}

/* a system value of the ray tracing stages: its built-in, its type and how dxil indexes it. */
typedef struct RaySystemValue {
    uint16_t dx;
    uint16_t builtin;
    /* 0 a u32, 1 a float, 2 a column of a uvec3, 3 of a vec3, 4 an element of a 4x3 matrix by row and column. */
    uint8_t shape;
} RaySystemValue;

static const RaySystemValue RAY_SYSTEM_VALUES[] = {
    {141, SpvBuiltInInstanceCustomIndexKHR, 0},
    {142, SpvBuiltInInstanceId, 0},
    {143, SpvBuiltInHitKindKHR, 0},
    {144, SpvBuiltInIncomingRayFlagsKHR, 0},
    {145, SpvBuiltInLaunchIdKHR, 2},
    {146, SpvBuiltInLaunchSizeKHR, 2},
    {147, SpvBuiltInWorldRayOriginKHR, 3},
    {148, SpvBuiltInWorldRayDirectionKHR, 3},
    {149, SpvBuiltInObjectRayOriginKHR, 3},
    {150, SpvBuiltInObjectRayDirectionKHR, 3},
    {151, SpvBuiltInObjectToWorldKHR, 4},
    {152, SpvBuiltInWorldToObjectKHR, 4},
    {153, SpvBuiltInRayTminKHR, 1},
    {154, SpvBuiltInRayTmaxKHR, 1},
    {161, SpvBuiltInPrimitiveId, 0},
    {213, SpvBuiltInRayGeometryIndexKHR, 0},
};

static int system_value(Compiler *c, const DxInst *ins, const RaySystemValue *sv)
{
    static const uint32_t SHAPES = 5;
    uint32_t types[SHAPES];
    types[0] = c->t_u32;
    types[1] = c->t_f32;
    types[2] = c->t_uvec[3];
    types[3] = c->t_vec[3];
    types[4] = spv_type_matrix(&c->b, c->t_vec[3], 4);
    uint32_t type = types[sv->shape];
    uint32_t v = spv_load(&c->b, type, dxil_builtin(c, sv->builtin, SpvStorageClassInput, type));
    if (sv->shape == 2 || sv->shape == 3) {
        uint32_t scalar = sv->shape == 2 ? c->t_u32 : c->t_f32;
        uint64_t col = dxil_constant(c, ins->ops[2], ~0ull);
        v = col < 3 ? dxil_extract(c, scalar, v, (uint32_t)col)
                    : spv_op2(&c->b, SpvOpVectorExtractDynamic, scalar, v, dxil_value(c, ins->ops[2]));
    } else if (sv->shape == 4) {
        uint64_t row = dxil_constant(c, ins->ops[2], ~0ull), col = dxil_constant(c, ins->ops[3], ~0ull);
        if (row >= 3 || col >= 4)
            return dxil_fail(c, "a ray matrix read at an index that is not constant");
        uint32_t e[3] = {v, (uint32_t)col, (uint32_t)row};
        v = spv_op(&c->b, SpvOpCompositeExtract, c->t_f32, e, 3);
    }
    dxil_set_plain(c, ins->value, v);
    return 0;
}

static int ray_stage_op(Compiler *c, const DxInst *ins, uint32_t op)
{
    switch (op) {
    case 155:
        spv_op_void(&c->b, SpvOpIgnoreIntersectionKHR, NULL, 0);
        c->b.block_open = 0;
        return 1;
    case 156:
        spv_op_void(&c->b, SpvOpTerminateRayKHR, NULL, 0);
        c->b.block_open = 0;
        return 1;
    case 157: return trace_ray(c, ins) ? -1 : 1;
    case 158: return report_hit(c, ins) ? -1 : 1;
    case 159: {
        uint32_t index = dxil_value(c, ins->ops[2]);
        return pass_through(c, ins->ops[3], SpvStorageClassCallableDataKHR, SpvOpExecuteCallableKHR, &index, 1) ? -1 : 1;
    }
    }
    for (uint32_t i = 0; i < sizeof(RAY_SYSTEM_VALUES) / sizeof(RAY_SYSTEM_VALUES[0]); i++)
        if (RAY_SYSTEM_VALUES[i].dx == op)
            return system_value(c, ins, &RAY_SYSTEM_VALUES[i]) ? -1 : 1;
    return 0;
}

/* ------------------------------------------------------------------ dx.op */

int dxil_ray_op(Compiler *c, const DxInst *ins, uint32_t op)
{
    if ((op >= 141 && op <= 161) || op == 213)
        return ray_stage_op(c, ins, op);
    if (op < OP_ALLOCATE || op > 215)
        return 0;
    if (!c->ray_queries)
        return dxil_fail(c, "a ray query without an allocation"), -1;
    switch (op) {
    case OP_ALLOCATE:
        for (uint32_t k = 0; k < c->ray_query_count; k++)
            if (&c->f->insts[c->ray_query_insts[k]] == ins) {
                dxil_set_plain(c, ins->value, dxil_uconst(c, k));
                return 1;
            }
        return dxil_fail(c, "a ray query allocated outside the entry point"), -1;
    case OP_TRACE: return trace(c, ins) ? -1 : 1;
    case OP_PROCEED:
        dxil_set_plain(c, ins->value, spv_op1(&c->b, SpvOpRayQueryProceedKHR, c->t_bool, query(c, ins->ops[2])));
        return 1;
    case OP_ABORT: {
        uint32_t q = query(c, ins->ops[2]);
        spv_op_void(&c->b, SpvOpRayQueryTerminateKHR, &q, 1);
        return 1;
    }
    case OP_COMMIT_TRIANGLE: {
        uint32_t q = query(c, ins->ops[2]);
        spv_op_void(&c->b, SpvOpRayQueryConfirmIntersectionKHR, &q, 1);
        return 1;
    }
    case OP_COMMIT_PROCEDURAL: {
        uint32_t w[2] = {query(c, ins->ops[2]), dxil_value(c, ins->ops[3])};
        spv_op_void(&c->b, SpvOpRayQueryGenerateIntersectionKHR, w, 2);
        return 1;
    }
    }
    for (uint32_t i = 0; i < sizeof(STATE_OPS) / sizeof(STATE_OPS[0]); i++)
        if (STATE_OPS[i].dx == op)
            return state(c, ins, &STATE_OPS[i]) ? -1 : 1;
    return 0;
}
