/*
 * signatures and what each stage reads and writes: dx.op's loadInput,
 * storeOutput and their kin, system values, and the execution modes.
 *
 * a signature element is a variable of its own at its packed place,
 * Location its row and Component its column, of 32-bit floats, signed or
 * unsigned integers as its component type says; system values are spir-v
 * built-ins. stages with inputs per vertex or control point, and the hull
 * shader's outputs, have an array of each around it.
 */
#include "internal.h"

enum { SIG_IN, SIG_OUT, SIG_PATCH };

static uint32_t t_i32(Compiler *c) { return spv_type_int(&c->b, 32, 1); }

static int read_signature(Compiler *c, const DxMetadata *list, Signature *sig)
{
    if (!list)
        return 0;
    sig->count = list->count;
    sig->elements = dxil_alloc(c, (list->count + 1) * sizeof(SigElement));
    if (!sig->elements)
        return -1;
    uint32_t clip = 0, cull = 0;
    for (uint32_t i = 0; i < list->count; i++) {
        const DxMetadata *e = dx_md_op(&c->m, list, i);
        SigElement *s = &sig->elements[i];
        if (!e)
            return dxil_fail(c, "a malformed signature element");
        s->id = (uint32_t)dx_md_int(&c->m, e, 0, i);
        s->name = dx_md_string(&c->m, e, 1);
        s->comp = (uint8_t)dx_md_int(&c->m, e, 2, COMP_F32);
        s->kind = (uint8_t)dx_md_int(&c->m, e, 3, SEM_ARBITRARY);
        s->semantic_index = (uint32_t)dx_md_int(&c->m, dx_md_op(&c->m, e, 4), 0, 0);
        s->interp = (uint8_t)dx_md_int(&c->m, e, 5, INTERP_UNDEFINED);
        s->rows = (uint32_t)dx_md_int(&c->m, e, 6, 1);
        s->cols = (uint32_t)dx_md_int(&c->m, e, 7, 1);
        s->row = (int32_t)dx_md_int(&c->m, e, 8, 0);
        s->col = (uint32_t)dx_md_int(&c->m, e, 9, 0);
        if (!s->rows || !s->cols || s->cols > 4)
            return dxil_fail(c, "a malformed signature element");
        if (s->kind == SEM_CLIP_DISTANCE) {
            s->distance_base = clip;
            clip += s->rows * s->cols;
        } else if (s->kind == SEM_CULL_DISTANCE) {
            s->distance_base = cull;
            cull += s->rows * s->cols;
        }
    }
    return 0;
}

int dxil_read_signatures(Compiler *c, const DxMetadata *list)
{
    if (!list)
        return 0;
    return read_signature(c, dx_md_op(&c->m, list, 0), &c->inputs) ||
           read_signature(c, dx_md_op(&c->m, list, 1), &c->outputs) ||
           read_signature(c, dx_md_op(&c->m, list, 2), &c->patch);
}

static SigElement *element(Compiler *c, Signature *sig, uint32_t id)
{
    for (uint32_t i = 0; i < sig->count; i++)
        if (sig->elements[i].id == id)
            return &sig->elements[i];
    if (id < sig->count)
        return &sig->elements[id];
    dxil_fail(c, "a signature element that does not exist");
    return NULL;
}

/* the 32-bit type an element has in the interface. */
static uint32_t interface_scalar(Compiler *c, uint8_t comp)
{
    switch (comp) {
    case COMP_F16:
    case COMP_F32:
    case COMP_SNORM_F16:
    case COMP_UNORM_F16:
    case COMP_SNORM_F32:
    case COMP_UNORM_F32: return c->t_f32;
    case COMP_I16:
    case COMP_I32:
    case COMP_I64: return t_i32(c);
    default: return c->t_u32;
    }
}

/* the vertices of the primitive a geometry shader takes. */
static uint32_t gs_vertices(uint32_t primitive)
{
    switch (primitive) {
    case 1: return 1;
    case 2: return 2;
    case 3: return 3;
    case 6: return 4;
    case 7: return 6;
    default: return primitive >= 8 && primitive <= 40 ? primitive - 7 : 1;
    }
}

/* how many vertices or control points an arrayed interface holds; 0 when it is not arrayed. */
static uint32_t arrayed(Compiler *c, int sig)
{
    if (sig == SIG_IN) {
        if (c->kind == KIND_GEOMETRY)
            return gs_vertices(c->gs_input);
        if (c->kind == KIND_HULL || c->kind == KIND_DOMAIN)
            return c->cp_in ? c->cp_in : 1;
    }
    if (sig == SIG_OUT && c->kind == KIND_HULL)
        return c->cp_out ? c->cp_out : 1;
    if (c->kind == KIND_MESH)
        return sig == SIG_OUT ? (c->mesh_vertices ? c->mesh_vertices : 1) : (c->mesh_primitives ? c->mesh_primitives : 1);
    return 0;
}

static uint32_t storage_of(Compiler *c, int sig)
{
    if (sig == SIG_PATCH)
        return c->kind == KIND_HULL || c->kind == KIND_MESH ? SpvStorageClassOutput : SpvStorageClassInput;
    return sig == SIG_IN ? SpvStorageClassInput : SpvStorageClassOutput;
}

static void interpolation(Compiler *c, uint32_t var, const SigElement *e, int integer)
{
    switch (e->interp) {
    case INTERP_CONSTANT: spv_decorate(&c->b, var, SpvDecorationFlat, NULL, 0); return;
    case INTERP_LINEAR_CENTROID: spv_decorate(&c->b, var, SpvDecorationCentroid, NULL, 0); break;
    case INTERP_NOPERSPECTIVE: spv_decorate(&c->b, var, SpvDecorationNoPerspective, NULL, 0); break;
    case INTERP_NOPERSPECTIVE_CENTROID:
        spv_decorate(&c->b, var, SpvDecorationNoPerspective, NULL, 0);
        spv_decorate(&c->b, var, SpvDecorationCentroid, NULL, 0);
        break;
    case INTERP_LINEAR_SAMPLE:
        spv_capability(&c->b, SpvCapabilitySampleRateShading);
        spv_decorate(&c->b, var, SpvDecorationSample, NULL, 0);
        break;
    case INTERP_NOPERSPECTIVE_SAMPLE:
        spv_capability(&c->b, SpvCapabilitySampleRateShading);
        spv_decorate(&c->b, var, SpvDecorationNoPerspective, NULL, 0);
        spv_decorate(&c->b, var, SpvDecorationSample, NULL, 0);
        break;
    default: break;
    }
    if (integer)
        spv_decorate(&c->b, var, SpvDecorationFlat, NULL, 0);
}

static uint32_t distance_count(const Signature *sig, uint8_t kind)
{
    uint32_t n = 0;
    for (uint32_t i = 0; i < sig->count; i++)
        if (sig->elements[i].kind == kind)
            n += sig->elements[i].rows * sig->elements[i].cols;
    return n;
}

/*
 * the built-in of a system value element, its type, and how its
 * components are indexed: 0 a scalar, 1 a vector by column, 2 an array of
 * scalars by row, 3 the clip or cull distances.
 */
static int element_builtin(Compiler *c, int sig, const SigElement *e, uint32_t *which, uint32_t *type, int *shape)
{
    int pixel = c->kind == KIND_PIXEL, in = sig == SIG_IN;
    /* the outputs that reach the rasterizer, and those that take a built-in there. */
    int raster = sig == SIG_OUT && (c->kind == KIND_VERTEX || c->kind == KIND_DOMAIN || c->kind == KIND_GEOMETRY);
    /* a mesh shader's primitive outputs take the built-ins of primitives. */
    int primitive = sig == SIG_PATCH && c->kind == KIND_MESH;
    *shape = 0;
    *type = t_i32(c);
    switch (e->kind) {
    case SEM_POSITION:
        if (c->kind == KIND_VERTEX && in)
            return 0;
        *type = c->t_vec[4];
        *shape = 1;
        *which = pixel && in ? SpvBuiltInFragCoord : SpvBuiltInPosition;
        return 1;
    case SEM_CLIP_DISTANCE:
    case SEM_CULL_DISTANCE: {
        if (c->kind == KIND_VERTEX && in)
            return 0;
        int clip = e->kind == SEM_CLIP_DISTANCE;
        spv_capability(&c->b, clip ? SpvCapabilityClipDistance : SpvCapabilityCullDistance);
        const Signature *s = in ? &c->inputs : &c->outputs;
        *type = spv_type_array(&c->b, c->t_f32, distance_count(s, e->kind));
        *shape = 3;
        *which = clip ? SpvBuiltInClipDistance : SpvBuiltInCullDistance;
        return 1;
    }
    case SEM_PRIMITIVE_ID:
        if (!(pixel && in) && !(raster && c->kind == KIND_GEOMETRY) && !primitive)
            return 0;
        spv_capability(&c->b, SpvCapabilityGeometry);
        *which = SpvBuiltInPrimitiveId;
        return 1;
    case SEM_RENDER_TARGET_ARRAY_INDEX:
        if (!(pixel && in) && !raster && !primitive)
            return 0;
        spv_capability(&c->b, pixel || c->kind == KIND_GEOMETRY ? SpvCapabilityGeometry : SpvCapabilityShaderLayer);
        *which = SpvBuiltInLayer;
        return 1;
    case SEM_VIEWPORT_ARRAY_INDEX:
        if (!(pixel && in) && !raster && !primitive)
            return 0;
        spv_capability(&c->b, pixel || c->kind == KIND_GEOMETRY ? SpvCapabilityMultiViewport
                                                                : SpvCapabilityShaderViewportIndex);
        *which = SpvBuiltInViewportIndex;
        return 1;
    case SEM_SAMPLE_INDEX:
        if (!(pixel && in))
            return 0;
        spv_capability(&c->b, SpvCapabilitySampleRateShading);
        *which = SpvBuiltInSampleId;
        return 1;
    case SEM_IS_FRONT_FACE:
        if (!(pixel && in))
            return 0;
        *type = c->t_bool;
        *which = SpvBuiltInFrontFacing;
        return 1;
    case SEM_COVERAGE:
        if (!pixel)
            return 0;
        *type = spv_type_array(&c->b, c->t_u32, 1);
        *shape = 2;
        *which = SpvBuiltInSampleMask;
        return 1;
    case SEM_DEPTH:
    case SEM_DEPTH_LESS_EQUAL:
    case SEM_DEPTH_GREATER_EQUAL:
        if (!pixel || in)
            return 0;
        *type = c->t_f32;
        c->depth_mode = e->kind;
        *which = SpvBuiltInFragDepth;
        return 1;
    case SEM_STENCIL_REF:
        if (!pixel || in)
            return 0;
        spv_capability(&c->b, SpvCapabilityStencilExportEXT);
        spv_extension(&c->b, "SPV_EXT_shader_stencil_export");
        *which = SpvBuiltInFragStencilRefEXT;
        return 1;
    case SEM_TESS_FACTOR:
    case SEM_INSIDE_TESS_FACTOR: {
        if (sig != SIG_PATCH)
            return 0;
        int inner = e->kind == SEM_INSIDE_TESS_FACTOR;
        *type = spv_type_array(&c->b, c->t_f32, inner ? 2 : 4);
        *shape = 2;
        *which = inner ? SpvBuiltInTessLevelInner : SpvBuiltInTessLevelOuter;
        return 1;
    }
    case SEM_CULL_PRIMITIVE:
        if (!primitive)
            return 0;
        *type = c->t_bool;
        *which = SpvBuiltInCullPrimitiveEXT;
        return 1;
    case SEM_VIEW_ID:
        if (!in)
            return 0;
        spv_capability(&c->b, SpvCapabilityMultiView);
        *which = SpvBuiltInViewIndex;
        return 1;
    default: return 0;
    }
}

static int same_semantic(const char *a, const char *b)
{
    if (!a || !b)
        return 0;
    for (; *a && *b; a++, b++) {
        char x = *a >= 'a' && *a <= 'z' ? *a - 32 : *a, y = *b >= 'a' && *b <= 'z' ? *b - 32 : *b;
        if (x != y)
            return 0;
    }
    return *a == *b;
}

/* the element of a linked signature with the same semantic, or NULL. */
static const DxbcSignatureElement *linked(const DxbcSignature *sig, const SigElement *e)
{
    for (uint32_t i = 0; sig && i < sig->count; i++)
        if (sig->elements[i].semantic_index == e->semantic_index && same_semantic(sig->elements[i].name, e->name))
            return &sig->elements[i];
    return NULL;
}

/* the location variable of an element; 0 for a mesh shader's output no pixel shader reads. */
static void location_variable(Compiler *c, int sig, const SigElement *e, uint32_t n, uint32_t *var, uint32_t *scalar)
{
    uint32_t storage = storage_of(c, sig);
    *scalar = interface_scalar(c, e->comp);
    /* pixel shader targets are at their render target, the semantic index; the rest at their row. */
    uint32_t location = c->kind == KIND_PIXEL && sig == SIG_OUT ? e->semantic_index : (uint32_t)e->row;
    uint32_t component = e->col;
    if (c->kind == KIND_MESH && c->options->linked_inputs) {
        const DxbcSignatureElement *to = linked(c->options->linked_inputs, e);
        if (!to) {
            *var = 0;
            return;
        }
        location = to->reg;
        component = to->mask ? (uint32_t)__builtin_ctz(to->mask) : 0;
    }
    uint32_t row_type = e->cols == 1 ? *scalar : spv_type_vector(&c->b, *scalar, e->cols);
    uint32_t type = e->rows > 1 ? spv_type_array(&c->b, row_type, e->rows) : row_type;
    *var = dxil_global(c, storage, n ? spv_type_array(&c->b, type, n) : type, 0);
    dxil_decorate(c, *var, SpvDecorationLocation, location);
    if (component)
        dxil_decorate(c, *var, SpvDecorationComponent, component);
    if (sig == SIG_PATCH)
        spv_decorate(&c->b, *var, c->kind == KIND_MESH ? SpvDecorationPerPrimitiveEXT : SpvDecorationPatch, NULL, 0);
    if (c->kind == KIND_PIXEL && sig == SIG_IN) {
        if (linked(c->options->per_primitive, e)) {
            spv_capability(&c->b, SpvCapabilityMeshShadingEXT);
            spv_extension(&c->b, "SPV_EXT_mesh_shader");
            spv_decorate(&c->b, *var, SpvDecorationPerPrimitiveEXT, NULL, 0);
            if (*scalar != c->t_f32)
                spv_decorate(&c->b, *var, SpvDecorationFlat, NULL, 0);
        } else {
            interpolation(c, *var, e, *scalar != c->t_f32);
        }
    }
}

/* the variable of an element, made at its first access. */
static int make_variable(Compiler *c, int sig, SigElement *e)
{
    if (e->var)
        return 0;
    uint32_t storage = storage_of(c, sig), n = arrayed(c, sig);
    int shape;
    uint32_t type, which;
    if (element_builtin(c, sig, e, &which, &type, &shape)) {
        e->builtin = 1;
        e->shape = (uint8_t)shape;
        e->scalar = shape == 1 || shape == 2 || shape == 3 ? c->t_f32 : type;
        if (e->kind == SEM_COVERAGE)
            e->scalar = c->t_u32;
        e->row_type = type;
        e->var_type = n ? spv_type_array(&c->b, type, n) : type;
        e->var = n || sig == SIG_PATCH ? dxil_global(c, storage, e->var_type, 0)
                                       : dxil_builtin(c, which, storage, e->var_type);
        if (n || sig == SIG_PATCH) {
            dxil_decorate(c, e->var, SpvDecorationBuiltIn, which);
            if (sig == SIG_PATCH)
                spv_decorate(&c->b, e->var, c->kind == KIND_MESH ? SpvDecorationPerPrimitiveEXT : SpvDecorationPatch,
                             NULL, 0);
        }
        /* a value the rasterizer takes may also be the next stage's input. */
        if (sig == SIG_OUT && e->row >= 0 && shape == 0 && which != SpvBuiltInFragDepth &&
            which != SpvBuiltInFragStencilRefEXT)
            location_variable(c, sig, e, n, &e->loc_var, &e->loc_scalar);
        /* elements sharing a built-in share its variable. */
        Signature *s = sig == SIG_IN ? &c->inputs : sig == SIG_OUT ? &c->outputs : &c->patch;
        for (uint32_t i = 0; i < s->count; i++) {
            SigElement *o = &s->elements[i];
            if (o != e && o->kind == e->kind && !o->var && !e->loc_var) {
                o->var = e->var;
                o->builtin = e->builtin;
                o->shape = e->shape;
                o->scalar = e->scalar;
                o->row_type = e->row_type;
                o->var_type = e->var_type;
            }
        }
        return 0;
    }
    if (e->row < 0)
        return dxil_fail(c, "a system value not implemented");
    location_variable(c, sig, e, n, &e->var, &e->scalar);
    e->dropped = !e->var;
    return 0;
}

/* the value of an index operand as a u32 id. */
static uint32_t index_of(Compiler *c, uint32_t id)
{
    const DxValue *v = dx_value(&c->m, c->f, id);
    if (!v || v->kind == DV_UNDEF)
        return dxil_uconst(c, 0);
    return dxil_value(c, id);
}

/* a pointer to one component of an element; vertex is an id, or 0 where the interface is not arrayed. */
static uint32_t component_pointer(Compiler *c, int sig, SigElement *e, uint32_t var, uint32_t scalar, int builtin,
                                  uint32_t vertex, uint32_t row_id, uint32_t col)
{
    uint32_t storage = storage_of(c, sig);
    uint32_t w[4] = {0, 0, 0, 0}, n = 0;
    if (arrayed(c, sig))
        w[n++] = vertex ? vertex : dxil_uconst(c, 0);
    if (builtin) {
        switch (e->shape) {
        case 1: w[n++] = dxil_uconst(c, col); break;
        case 2: w[n++] = row_id; break;
        case 3: {
            uint32_t at = e->distance_base + col;
            uint32_t k = e->cols == 1 ? row_id : spv_op2(&c->b, SpvOpIMul, c->t_u32, row_id, dxil_uconst(c, e->cols));
            w[n++] = spv_op2(&c->b, SpvOpIAdd, c->t_u32, k, dxil_uconst(c, at));
            break;
        }
        default: break;
        }
    } else {
        if (e->rows > 1)
            w[n++] = row_id;
        if (e->cols > 1)
            w[n++] = dxil_uconst(c, col);
    }
    if (!n)
        return var;
    uint32_t a[5] = {var, w[0], w[1], w[2], w[3]};
    return spv_op(&c->b, SpvOpAccessChain, spv_type_pointer(&c->b, storage, scalar), a, n + 1);
}

static uint32_t builtin_load(Compiler *c, uint32_t which, uint32_t type)
{
    return spv_load(&c->b, type, dxil_builtin(c, which, SpvStorageClassInput, type));
}

/* a vertex shader's index, without the draw's base as direct3d counts. */
static uint32_t draw_index(Compiler *c, uint32_t index, uint32_t base)
{
    spv_capability(&c->b, SpvCapabilityDrawParameters);
    uint32_t i = builtin_load(c, index, t_i32(c)), b = builtin_load(c, base, t_i32(c));
    return spv_op1(&c->b, SpvOpBitcast, c->t_u32, spv_op2(&c->b, SpvOpISub, t_i32(c), i, b));
}

static int load_element(Compiler *c, const DxInst *ins, int sig, uint32_t vertex)
{
    Signature *s = sig == SIG_IN ? &c->inputs : sig == SIG_OUT ? &c->outputs : &c->patch;
    SigElement *e = element(c, s, (uint32_t)dxil_constant(c, ins->ops[2], 0));
    if (!e)
        return -1;
    uint32_t type = dxil_type(c, ins->type);
    uint32_t col = (uint32_t)dxil_constant(c, ins->ops[4], 0);
    uint32_t value;
    if (sig == SIG_IN && c->kind == KIND_VERTEX && (e->kind == SEM_VERTEX_ID || e->kind == SEM_INSTANCE_ID)) {
        value = e->kind == SEM_VERTEX_ID ? draw_index(c, SpvBuiltInVertexIndex, SpvBuiltInBaseVertex)
                                         : draw_index(c, SpvBuiltInInstanceIndex, SpvBuiltInBaseInstance);
        dxil_set_plain(c, ins->value, dxil_convert(c, value, c->t_u32, type));
        return 0;
    }
    if (make_variable(c, sig, e))
        return -1;
    if (e->dropped) {
        dxil_set_plain(c, ins->value, spv_const_null(&c->b, type));
        return 0;
    }
    uint32_t ptr = component_pointer(c, sig, e, e->var, e->scalar, e->builtin != 0, vertex, index_of(c, ins->ops[3]), col);
    value = spv_load(&c->b, e->scalar, ptr);
    if (e->kind == SEM_POSITION && c->kind == KIND_PIXEL && col == 3)
        /* direct3d's w is the clip w, vulkan's its reciprocal. */
        value = spv_op2(&c->b, SpvOpFDiv, c->t_f32, dxil_fconst(c, 1.0f), value);
    if (e->scalar == c->t_bool && type != c->t_bool)
        value = spv_op3(&c->b, SpvOpSelect, type, value, spv_const(&c->b, type, ~0u), spv_const_null(&c->b, type));
    else
        value = dxil_convert(c, value, e->scalar, type);
    dxil_set_plain(c, ins->value, value);
    return 0;
}

static int store_element(Compiler *c, const DxInst *ins, int sig, uint32_t value_op, uint32_t vertex)
{
    Signature *s = sig == SIG_OUT ? &c->outputs : &c->patch;
    SigElement *e = element(c, s, (uint32_t)dxil_constant(c, ins->ops[2], 0));
    if (!e || make_variable(c, sig, e))
        return -1;
    if (e->dropped)
        return 0;
    const DxValue *v = dx_value(&c->m, c->f, ins->ops[value_op]);
    if (!v)
        return dxil_fail(c, "a store of nothing");
    uint32_t col = (uint32_t)dxil_constant(c, ins->ops[4], 0);
    uint32_t original = dxil_value(c, ins->ops[value_op]);
    uint32_t from = dxil_type(c, v->type);
    uint32_t value = from == c->t_bool ? spv_op3(&c->b, SpvOpSelect, e->scalar, original, spv_const(&c->b, e->scalar, ~0u),
                                                 spv_const_null(&c->b, e->scalar))
                                       : dxil_convert(c, original, from, e->scalar);
    /* the tessellator takes factors up to the hull shader's maximum. */
    if ((e->kind == SEM_TESS_FACTOR || e->kind == SEM_INSIDE_TESS_FACTOR) && c->max_tess_factor) {
        uint32_t args[2] = {value, spv_const(&c->b, c->t_f32, c->max_tess_factor)};
        value = spv_glsl(&c->b, c->t_f32, GLSLstd450NMin, args, 2);
    }
    uint32_t row = index_of(c, ins->ops[3]);
    spv_store(&c->b, component_pointer(c, sig, e, e->var, e->scalar, e->builtin != 0, vertex, row, col), value);
    if (e->loc_var) {
        value = from == c->t_bool ? spv_op3(&c->b, SpvOpSelect, e->loc_scalar, original,
                                            spv_const(&c->b, e->loc_scalar, ~0u), spv_const_null(&c->b, e->loc_scalar))
                                  : dxil_convert(c, original, from, e->loc_scalar);
        spv_store(&c->b, component_pointer(c, sig, e, e->loc_var, e->loc_scalar, 0, vertex, row, col), value);
    }
    return 0;
}

static uint32_t invocation_id(Compiler *c)
{
    return spv_op1(&c->b, SpvOpBitcast, c->t_u32, builtin_load(c, SpvBuiltInInvocationId, t_i32(c)));
}

/* a component of a vector built-in, as the call's type. */
static int builtin_component(Compiler *c, const DxInst *ins, uint32_t which, uint32_t size)
{
    uint32_t type = spv_type_vector(&c->b, c->t_u32, size);
    uint32_t v = builtin_load(c, which, type);
    uint64_t k = dxil_constant(c, ins->ops[2], ~0ull);
    uint32_t r = k < size ? dxil_extract(c, c->t_u32, v, (uint32_t)k)
                          : spv_op2(&c->b, SpvOpVectorExtractDynamic, c->t_u32, v, dxil_value(c, ins->ops[2]));
    dxil_set_plain(c, ins->value, dxil_convert(c, r, c->t_u32, dxil_type(c, ins->type)));
    return 0;
}

static int builtin_scalar(Compiler *c, const DxInst *ins, uint32_t which, uint32_t type)
{
    uint32_t v = builtin_load(c, which, type);
    if (type != c->t_u32)
        v = type == c->t_bool ? spv_op3(&c->b, SpvOpSelect, c->t_u32, v, dxil_uconst(c, 1), dxil_uconst(c, 0))
                              : spv_op1(&c->b, SpvOpBitcast, c->t_u32, v);
    uint32_t to = dxil_type(c, ins->type);
    dxil_set_plain(c, ins->value, to == c->t_bool ? spv_op2(&c->b, SpvOpINotEqual, to, v, dxil_uconst(c, 0))
                                                  : dxil_convert(c, v, c->t_u32, to));
    return 0;
}

/* evalCentroid, evalSampleIndex and evalSnapped: an input interpolated elsewhere. */
static int evaluate(Compiler *c, const DxInst *ins, uint32_t op)
{
    SigElement *e = element(c, &c->inputs, (uint32_t)dxil_constant(c, ins->ops[2], 0));
    if (!e || make_variable(c, SIG_IN, e))
        return -1;
    spv_capability(&c->b, SpvCapabilityInterpolationFunction);
    uint32_t col = (uint32_t)dxil_constant(c, ins->ops[4], 0);
    uint32_t ptr = component_pointer(c, SIG_IN, e, e->var, e->scalar, e->builtin != 0, 0, index_of(c, ins->ops[3]), col);
    uint32_t args[2] = {ptr, 0}, count = 1, instruction = GLSLstd450InterpolateAtCentroid;
    if (op == 88) {
        instruction = GLSLstd450InterpolateAtSample;
        args[count++] = spv_op1(&c->b, SpvOpBitcast, t_i32(c), dxil_value(c, ins->ops[5]));
    } else if (op == 87) {
        /* the offset is in sixteenths of a pixel, from -8 to 7. */
        instruction = GLSLstd450InterpolateAtOffset;
        uint32_t parts[2];
        for (int i = 0; i < 2; i++) {
            uint32_t s = spv_op1(&c->b, SpvOpConvertSToF, c->t_f32, dxil_value(c, ins->ops[5 + i]));
            parts[i] = spv_op2(&c->b, SpvOpFMul, c->t_f32, s, dxil_fconst(c, 1.0f / 16.0f));
        }
        args[count++] = dxil_vector(c, c->t_f32, parts, 2);
    }
    uint32_t v = spv_glsl(&c->b, e->scalar, instruction, args, count);
    dxil_set_plain(c, ins->value, dxil_convert(c, v, e->scalar, dxil_type(c, ins->type)));
    return 0;
}

static int discard(Compiler *c, const DxInst *ins)
{
    spv_capability(&c->b, SpvCapabilityDemoteToHelperInvocation);
    c->info->uses_discard = 1;
    uint64_t always = dxil_constant(c, ins->ops[2], 0);
    if (always & 1) {
        spv_op_void(&c->b, SpvOpDemoteToHelperInvocation, NULL, 0);
        return 0;
    }
    uint32_t merge;
    dxil_begin_if(c, dxil_value(c, ins->ops[2]), &merge);
    spv_op_void(&c->b, SpvOpDemoteToHelperInvocation, NULL, 0);
    dxil_end_if(c, merge);
    return 0;
}

static int emit_stream(Compiler *c, const DxInst *ins, int emit, int cut)
{
    uint32_t stream = (uint32_t)dxil_constant(c, ins->ops[2], 0);
    if (stream) {
        spv_capability(&c->b, SpvCapabilityGeometryStreams);
        uint32_t id = dxil_uconst(c, stream);
        if (emit)
            spv_op_void(&c->b, SpvOpEmitStreamVertex, &id, 1);
        if (cut)
            spv_op_void(&c->b, SpvOpEndStreamPrimitive, &id, 1);
        return 0;
    }
    if (emit)
        spv_op_void(&c->b, SpvOpEmitVertex, NULL, 0);
    if (cut)
        spv_op_void(&c->b, SpvOpEndPrimitive, NULL, 0);
    return 0;
}

/* ------------------------------------------------------------------ mesh and task shaders */

/* emitIndices: a primitive's vertices, in the built-in array of triangles or lines. */
static int emit_indices(Compiler *c, const DxInst *ins)
{
    uint32_t n = c->mesh_topology == 1 ? 2 : 3;
    uint32_t vec = c->t_uvec[n];
    uint32_t which = n == 2 ? SpvBuiltInPrimitiveLineIndicesEXT : SpvBuiltInPrimitiveTriangleIndicesEXT;
    uint32_t array = spv_type_array(&c->b, vec, c->mesh_primitives ? c->mesh_primitives : 1);
    uint32_t var = dxil_builtin(c, which, SpvStorageClassOutput, array);
    uint32_t parts[3];
    for (uint32_t i = 0; i < n; i++)
        parts[i] = dxil_value(c, ins->ops[3 + i]);
    uint32_t w[2] = {var, dxil_value(c, ins->ops[2])};
    uint32_t ptr = spv_op(&c->b, SpvOpAccessChain, spv_type_pointer(&c->b, SpvStorageClassOutput, vec), w, 2);
    spv_store(&c->b, ptr, dxil_vector(c, c->t_u32, parts, n));
    return 0;
}

/* the task shader's payload, made at the first use; type is the dxil type of what it holds. */
static uint32_t payload(Compiler *c, uint32_t type)
{
    if (!c->payload) {
        c->payload_type = dxil_type(c, type);
        c->payload = dxil_global(c, SpvStorageClassTaskPayloadWorkgroupEXT, c->payload_type, 0);
    }
    return c->payload;
}

/* getMeshPayload: a pointer to what the task shader passed. */
static int mesh_payload(Compiler *c, const DxInst *ins)
{
    const DxType *t = &c->m.types[ins->type];
    if (t->kind != DT_POINTER)
        return dxil_fail(c, "a mesh payload that is not a pointer");
    uint32_t var = payload(c, t->element);
    dxil_set_pointer(c, ins->value, var, c->payload_type, SpvStorageClassTaskPayloadWorkgroupEXT);
    return 0;
}

/* dispatchMesh: the counts and payload the task shader's returns emit. */
static int dispatch_mesh(Compiler *c, const DxInst *ins)
{
    for (uint32_t i = 0; i < 3; i++)
        spv_store(&c->b, c->task_counts[i], dxil_value(c, ins->ops[2 + i]));
    uint32_t type, value = dxil_load_pointer(c, ins->ops[5], &type);
    if (!value || !c->payload || type != c->payload_type)
        return dxil_fail(c, "a mesh payload of another type");
    spv_store(&c->b, c->payload, value);
    return 0;
}

int dxil_prepare_task(Compiler *c, const DxFunction *f)
{
    for (uint32_t i = 0; i < 3; i++)
        c->task_counts[i] = dxil_global(c, SpvStorageClassPrivate, c->t_u32, dxil_uconst(c, 0));
    for (uint32_t i = 0; i < f->inst_count; i++) {
        const DxInst *ins = &f->insts[i];
        if (ins->op != DI_CALL || ins->count < 6)
            continue;
        const DxValue *op = dx_value(&c->m, f, ins->ops[1]);
        const DxValue *p = dx_value(&c->m, f, ins->ops[5]);
        if (!op || op->kind != DV_INT || op->bits != 173 || !p)
            continue;
        const DxType *t = &c->m.types[p->type];
        if (t->kind == DT_POINTER)
            payload(c, t->element);
    }
    return c->error ? -1 : 0;
}

void dxil_emit_tasks(Compiler *c)
{
    uint32_t w[4], n = 3;
    for (uint32_t i = 0; i < 3; i++)
        w[i] = spv_load(&c->b, c->t_u32, c->task_counts[i]);
    if (c->payload)
        w[n++] = c->payload;
    spv_op_void(&c->b, SpvOpEmitMeshTasksEXT, w, n);
    c->b.block_open = 0;
}

int dxil_io_op(Compiler *c, const DxInst *ins, uint32_t op)
{
    switch (op) {
    case 4: /* loadInput */
        return load_element(c, ins, SIG_IN, ins->count > 5 && !dxil_is_undef(c, ins->ops[5]) ? dxil_value(c, ins->ops[5]) : 0) ? -1 : 1;
    case 5: /* storeOutput */
        return store_element(c, ins, SIG_OUT, 5, c->kind == KIND_HULL ? invocation_id(c) : 0) ? -1 : 1;
    case 103: /* loadOutputControlPoint */
        return load_element(c, ins, SIG_OUT, dxil_value(c, ins->ops[5])) ? -1 : 1;
    case 104: /* loadPatchConstant */
        return load_element(c, ins, SIG_PATCH, 0) ? -1 : 1;
    case 106: /* storePatchConstant */
        return store_element(c, ins, SIG_PATCH, 5, 0) ? -1 : 1;
    case 87:
    case 88:
    case 89: return evaluate(c, ins, op) ? -1 : 1;
    case 82: return discard(c, ins) ? -1 : 1;
    case 93: return builtin_component(c, ins, SpvBuiltInGlobalInvocationId, 3) ? -1 : 1;
    case 94: return builtin_component(c, ins, SpvBuiltInWorkgroupId, 3) ? -1 : 1;
    case 95: return builtin_component(c, ins, SpvBuiltInLocalInvocationId, 3) ? -1 : 1;
    case 96: return builtin_scalar(c, ins, SpvBuiltInLocalInvocationIndex, c->t_u32) ? -1 : 1;
    case 105: { /* domainLocation */
        uint32_t v = builtin_load(c, SpvBuiltInTessCoord, spv_type_vector(&c->b, c->t_f32, 3));
        uint32_t k = (uint32_t)dxil_constant(c, ins->ops[2], 0);
        dxil_set_plain(c, ins->value, dxil_convert(c, dxil_extract(c, c->t_f32, v, k < 3 ? k : 0), c->t_f32,
                                                   dxil_type(c, ins->type)));
        return 1;
    }
    case 100: /* gsInstanceID */
    case 107: /* outputControlPointID */
        return builtin_scalar(c, ins, SpvBuiltInInvocationId, t_i32(c)) ? -1 : 1;
    case 108: /* primitiveID */
        if (c->kind == KIND_PIXEL)
            spv_capability(&c->b, SpvCapabilityGeometry);
        return builtin_scalar(c, ins, SpvBuiltInPrimitiveId, t_i32(c)) ? -1 : 1;
    case 90: /* sampleIndex */
        spv_capability(&c->b, SpvCapabilitySampleRateShading);
        return builtin_scalar(c, ins, SpvBuiltInSampleId, t_i32(c)) ? -1 : 1;
    case 91: { /* coverage */
        uint32_t array = spv_type_array(&c->b, c->t_u32, 1);
        uint32_t var = dxil_builtin(c, SpvBuiltInSampleMask, SpvStorageClassInput, array);
        uint32_t zero = dxil_uconst(c, 0);
        uint32_t w[2] = {var, zero};
        uint32_t ptr = spv_op(&c->b, SpvOpAccessChain, spv_type_pointer(&c->b, SpvStorageClassInput, c->t_u32), w, 2);
        dxil_set_plain(c, ins->value, spv_load(&c->b, c->t_u32, ptr));
        return 1;
    }
    case 138: /* viewID */
        spv_capability(&c->b, SpvCapabilityMultiView);
        return builtin_scalar(c, ins, SpvBuiltInViewIndex, t_i32(c)) ? -1 : 1;
    case 221: { /* isHelperLane */
        spv_capability(&c->b, SpvCapabilityDemoteToHelperInvocation);
        uint32_t v = c->kind == KIND_PIXEL ? spv_op(&c->b, SpvOpIsHelperInvocationEXT, c->t_bool, NULL, 0)
                                           : spv_const_bool(&c->b, 0);
        dxil_set_plain(c, ins->value, v);
        return 1;
    }
    case 97: return emit_stream(c, ins, 1, 0) ? -1 : 1;
    case 98: return emit_stream(c, ins, 0, 1) ? -1 : 1;
    case 99: return emit_stream(c, ins, 1, 1) ? -1 : 1;
    case 256: /* startVertexLocation */
        spv_capability(&c->b, SpvCapabilityDrawParameters);
        return builtin_scalar(c, ins, SpvBuiltInBaseVertex, t_i32(c)) ? -1 : 1;
    case 257: /* startInstanceLocation */
        spv_capability(&c->b, SpvCapabilityDrawParameters);
        return builtin_scalar(c, ins, SpvBuiltInBaseInstance, t_i32(c)) ? -1 : 1;
    case 168: { /* setMeshOutputCounts */
        uint32_t w[2] = {dxil_value(c, ins->ops[2]), dxil_value(c, ins->ops[3])};
        spv_op_void(&c->b, SpvOpSetMeshOutputsEXT, w, 2);
        return 1;
    }
    case 169: return emit_indices(c, ins) ? -1 : 1;
    case 170: return mesh_payload(c, ins) ? -1 : 1;
    case 171: return store_element(c, ins, SIG_OUT, 5, dxil_value(c, ins->ops[6])) ? -1 : 1;
    case 172: return store_element(c, ins, SIG_PATCH, 5, dxil_value(c, ins->ops[6])) ? -1 : 1;
    case 173: return dispatch_mesh(c, ins) ? -1 : 1;
    case 77: { /* renderTargetGetSampleCount */
        uint32_t n = c->options->sample_count ? c->options->sample_count : 1;
        dxil_set_plain(c, ins->value, dxil_uconst(c, n));
        return 1;
    }
    default: return 0;
    }
}

/* ------------------------------------------------------------------ execution modes */

static void mode(Compiler *c, uint32_t main, uint32_t m)
{
    uint32_t w[2] = {main, m};
    spv_emit(&c->b, SECTION_EXECUTION_MODES, SpvOpExecutionMode, w, 2);
}

static void mode1(Compiler *c, uint32_t main, uint32_t m, uint32_t value)
{
    uint32_t w[3] = {main, m, value};
    spv_emit(&c->b, SECTION_EXECUTION_MODES, SpvOpExecutionMode, w, 3);
}

void dxil_execution_modes(Compiler *c, uint32_t main)
{
    switch (c->kind) {
    case KIND_PIXEL:
        mode(c, main, SpvExecutionModeOriginUpperLeft);
        if (c->depth_mode) {
            c->info->writes_depth = 1;
            mode(c, main, SpvExecutionModeDepthReplacing);
            if (c->depth_mode == SEM_DEPTH_GREATER_EQUAL)
                mode(c, main, SpvExecutionModeDepthGreater);
            else if (c->depth_mode == SEM_DEPTH_LESS_EQUAL)
                mode(c, main, SpvExecutionModeDepthLess);
        }
        /* the force early depth stencil flag. */
        if (c->flags & 8)
            mode(c, main, SpvExecutionModeEarlyFragmentTests);
        break;
    case KIND_GEOMETRY: {
        spv_capability(&c->b, SpvCapabilityGeometry);
        static const uint32_t INPUTS[8] = {0, SpvExecutionModeInputPoints, SpvExecutionModeInputLines,
                                           SpvExecutionModeTriangles, 0, 0, SpvExecutionModeInputLinesAdjacency,
                                           SpvExecutionModeInputTrianglesAdjacency};
        uint32_t input = c->gs_input < 8 && INPUTS[c->gs_input] ? INPUTS[c->gs_input] : SpvExecutionModeInputPoints;
        uint32_t output = c->gs_topology == 1   ? SpvExecutionModeOutputPoints
                          : c->gs_topology <= 3 ? SpvExecutionModeOutputLineStrip
                                                : SpvExecutionModeOutputTriangleStrip;
        mode(c, main, input);
        mode(c, main, output);
        mode1(c, main, SpvExecutionModeOutputVertices, c->gs_max_vertices ? c->gs_max_vertices : 1);
        mode1(c, main, SpvExecutionModeInvocations, c->gs_instances ? c->gs_instances : 1);
        break;
    }
    case KIND_HULL:
    case KIND_DOMAIN: {
        spv_capability(&c->b, SpvCapabilityTessellation);
        static const uint32_t DOMAINS[4] = {SpvExecutionModeTriangles, SpvExecutionModeIsolines,
                                            SpvExecutionModeTriangles, SpvExecutionModeQuads};
        mode(c, main, DOMAINS[c->tess_domain & 3]);
        if (c->kind == KIND_HULL) {
            mode1(c, main, SpvExecutionModeOutputVertices, c->cp_out ? c->cp_out : 1);
            static const uint32_t SPACING[5] = {SpvExecutionModeSpacingEqual, SpvExecutionModeSpacingEqual,
                                                SpvExecutionModeSpacingEqual, SpvExecutionModeSpacingFractionalOdd,
                                                SpvExecutionModeSpacingFractionalEven};
            mode(c, main, SPACING[c->tess_partitioning < 5 ? c->tess_partitioning : 0]);
            if (c->tess_primitive == 1)
                mode(c, main, SpvExecutionModePointMode);
            else if (c->tess_primitive == 3 || c->tess_primitive == 4)
                mode(c, main, c->tess_primitive == 4 ? SpvExecutionModeVertexOrderCcw : SpvExecutionModeVertexOrderCw);
        }
        break;
    }
    case KIND_MESH:
    case KIND_AMPLIFICATION:
    case KIND_COMPUTE: {
        /* vulkan wants at least one vertex and primitive, which a shader writing none never uses. */
        if (c->kind == KIND_MESH) {
            mode1(c, main, SpvExecutionModeOutputVertices, c->mesh_vertices ? c->mesh_vertices : 1);
            mode1(c, main, SpvExecutionModeOutputPrimitivesEXT, c->mesh_primitives ? c->mesh_primitives : 1);
            mode(c, main, c->mesh_topology == 1 ? SpvExecutionModeOutputLinesEXT : SpvExecutionModeOutputTrianglesEXT);
        }
        uint32_t w[5] = {main, SpvExecutionModeLocalSize, c->thread_group[0] ? c->thread_group[0] : 1,
                         c->thread_group[1] ? c->thread_group[1] : 1, c->thread_group[2] ? c->thread_group[2] : 1};
        spv_emit(&c->b, SECTION_EXECUTION_MODES, SpvOpExecutionMode, w, 5);
        break;
    }
    default: break;
    }
}
