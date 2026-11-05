#include "lower.h"

void lw_globals(Lower *l);
void lw_statics(Lower *l);

/* how a flat component of an entry parameter reaches the shader. */
typedef struct Slot {
    uint8_t kind;
    uint8_t comp;
    uint32_t reg;
    /* the operand type of system values with operands of their own. */
    uint32_t operand;
} Slot;

enum { SLOT_NONE, SLOT_REG, SLOT_SV };

static const struct {
    const char *name;
    uint32_t sysval;
} system_values[] = {
    {"SV_Position", SV_POSITION},
    {"SV_ClipDistance", SV_CLIP_DISTANCE},
    {"SV_CullDistance", SV_CULL_DISTANCE},
    {"SV_RenderTargetArrayIndex", SV_RENDER_TARGET_ARRAY_INDEX},
    {"SV_ViewportArrayIndex", SV_VIEWPORT_ARRAY_INDEX},
    {"SV_VertexID", SV_VERTEX_ID},
    {"SV_PrimitiveID", SV_PRIMITIVE_ID},
    {"SV_InstanceID", SV_INSTANCE_ID},
    {"SV_IsFrontFace", SV_IS_FRONT_FACE},
    {"SV_SampleIndex", SV_SAMPLE_INDEX},
    {"SV_Target", SV_TARGET},
    {"SV_Depth", SV_DEPTH},
    {"SV_Coverage", SV_COVERAGE},
    {"SV_DepthGreaterEqual", SV_DEPTH_GREATER_EQUAL},
    {"SV_DepthLessEqual", SV_DEPTH_LESS_EQUAL},
    {"SV_StencilRef", SV_STENCIL_REF},
    {"SV_InnerCoverage", SV_INNER_COVERAGE},
    {"SV_TessFactor", 0x100},
    {"SV_InsideTessFactor", 0x101},
};

/* splits TEXCOORD12 into TEXCOORD and 12. */
static const char *split_semantic(Lower *l, const char *semantic, uint32_t *index)
{
    size_t n = __builtin_strlen(semantic);
    size_t end = n;
    while (end && semantic[end - 1] >= '0' && semantic[end - 1] <= '9')
        end--;
    uint32_t v = 0;
    for (size_t i = end; i < n; i++)
        v = v * 10 + (uint32_t)(semantic[i] - '0');
    *index = v;
    return hl_intern(l->h, semantic, end);
}

static uint32_t sysval_of(const char *name)
{
    for (size_t i = 0; i < HL_ARRAY_SIZE(system_values); i++)
        if (!hl_stricmp(name, system_values[i].name))
            return system_values[i].sysval;
    return 0;
}

static bool is_sv(const char *name) { return !hl_strnicmp(name, "SV_", 3); }

static uint32_t component_type(uint8_t base)
{
    switch (hl_base_storage(base)) {
    case BT_UINT:
    case BT_BOOL:
        return 1;
    case BT_INT:
        return 2;
    default:
        return 3;
    }
}

/* an element before packing, and the components of the parameter it covers. */
typedef struct Pending {
    IrElement e;
    uint32_t first;
    uint32_t count;
    uint32_t operand;
    bool generated;
} Pending;

typedef struct PendingList {
    Pending *items;
    uint32_t count, capacity;
} PendingList;

static void add_element(Lower *l, PendingList *list, const char *semantic, uint32_t index_offset, const HlType *t,
                        uint32_t first, uint32_t interp, bool output, HlLoc loc)
{
    IrShader *s = l->s;
    uint32_t index;
    const char *name = split_semantic(l, semantic, &index);
    index += index_offset;
    uint8_t stage = s->profile.stage;
    Pending p = {.first = first, .count = hl_type_components(t)};
    p.e.name = name;
    p.e.index = index;
    p.e.component_type = component_type(t->base);
    p.e.mask = (uint8_t)((1u << (t->kind == TY_SCALAR ? 1 : t->cols)) - 1);
    p.e.interp = interp;
    uint32_t sv = is_sv(name) ? sysval_of(name) : 0;
    if (is_sv(name) && !sv) {
        /* system values that are operands of their own. */
        static const struct {
            const char *name;
            uint8_t stage;
            bool output;
            uint32_t operand;
        } operands[] = {
            {"SV_DispatchThreadID", STAGE_COMPUTE, false, OPERAND_INPUT_THREAD_ID},
            {"SV_GroupID", STAGE_COMPUTE, false, OPERAND_INPUT_THREAD_GROUP_ID},
            {"SV_GroupThreadID", STAGE_COMPUTE, false, OPERAND_INPUT_THREAD_ID_IN_GROUP},
            {"SV_GroupIndex", STAGE_COMPUTE, false, OPERAND_INPUT_THREAD_ID_IN_GROUP_FLATTENED},
            {"SV_GSInstanceID", STAGE_GEOMETRY, false, OPERAND_INPUT_GS_INSTANCE_ID},
            {"SV_OutputControlPointID", STAGE_HULL, false, OPERAND_OUTPUT_CONTROL_POINT_ID},
            {"SV_DomainLocation", STAGE_DOMAIN, false, OPERAND_INPUT_DOMAIN_POINT},
        };
        for (size_t i = 0; i < HL_ARRAY_SIZE(operands); i++)
            if (!hl_stricmp(name, operands[i].name) && operands[i].stage == stage && operands[i].output == output)
                p.operand = operands[i].operand;
        if (!p.operand) {
            hl_error(l->h, loc, 4502, "invalid %s semantic '%s'", output ? "output" : "input", semantic);
            l->failed = true;
            return;
        }
    }
    if (sv == SV_PRIMITIVE_ID && !output && stage != STAGE_PIXEL)
        p.operand = OPERAND_INPUT_PRIMITIVEID;
    if (stage == STAGE_PIXEL && !output && sv == SV_COVERAGE)
        p.operand = OPERAND_INPUT_COVERAGE_MASK;
    if (stage == STAGE_PIXEL && !output && sv == SV_INNER_COVERAGE)
        p.operand = OPERAND_INNER_COVERAGE;
    if (stage == STAGE_PIXEL && output) {
        switch (sv) {
        case SV_DEPTH: p.operand = OPERAND_OUTPUT_DEPTH; break;
        case SV_DEPTH_GREATER_EQUAL: p.operand = OPERAND_OUTPUT_DEPTH_GREATER_EQUAL; break;
        case SV_DEPTH_LESS_EQUAL: p.operand = OPERAND_OUTPUT_DEPTH_LESS_EQUAL; break;
        case SV_COVERAGE: p.operand = OPERAND_OUTPUT_COVERAGE_MASK; break;
        case SV_STENCIL_REF: p.operand = OPERAND_OUTPUT_STENCIL_REF; break;
        case SV_TARGET: break;
        default:
            if (sv) {
                hl_error(l->h, loc, 4502, "invalid output semantic '%s'", semantic);
                l->failed = true;
                return;
            }
        }
    }
    /* which system values the signature names: those the stage consumes or produces. */
    bool keep = false;
    if (stage == STAGE_VERTEX && !output)
        keep = sv == SV_VERTEX_ID || sv == SV_INSTANCE_ID;
    else if (stage == STAGE_PIXEL && !output)
        keep = sv && sv != SV_VERTEX_ID && sv != SV_INSTANCE_ID;
    else if (stage == STAGE_PIXEL && output)
        keep = sv == SV_TARGET || sv == SV_DEPTH || sv == SV_COVERAGE || sv == SV_DEPTH_GREATER_EQUAL ||
               sv == SV_DEPTH_LESS_EQUAL || sv == SV_STENCIL_REF;
    else if (output)
        keep = sv == SV_POSITION || sv == SV_CLIP_DISTANCE || sv == SV_CULL_DISTANCE ||
               sv == SV_RENDER_TARGET_ARRAY_INDEX || sv == SV_VIEWPORT_ARRAY_INDEX;
    else if (stage == STAGE_GEOMETRY || stage == STAGE_HULL || stage == STAGE_DOMAIN)
        keep = false;
    p.e.sysval = keep ? sv : 0;
    if (stage == STAGE_PIXEL && !output && sv == SV_POSITION)
        p.e.interp = IM_NOPERSPECTIVE | 0x100;
    p.generated = stage == STAGE_PIXEL && !output &&
                  (sv == SV_IS_FRONT_FACE || sv == SV_SAMPLE_INDEX || sv == SV_PRIMITIVE_ID);
    if (stage == STAGE_PIXEL && output && sv == SV_TARGET && index > 7) {
        hl_error(l->h, loc, 4502, "invalid output semantic '%s'", semantic);
        l->failed = true;
        return;
    }
    HL_PUSH(l->h, list->items, list->count, list->capacity, p);
}

/* elements of a parameter or field: structs by field, arrays and matrices by row. */
static void walk_type(Lower *l, PendingList *list, const HlType *t, const char *semantic, uint32_t *index_offset,
                      uint32_t *first, uint32_t interp, bool output, HlLoc loc, const char *what)
{
    switch (t->kind) {
    case TY_STRUCT:
        for (uint32_t i = 0; i < t->st->field_count; i++) {
            const HlField *f = &t->st->fields[i];
            uint32_t offset = 0;
            const char *s = f->semantic ? f->semantic : semantic;
            if (f->semantic && semantic)
                hl_warning(l->h, f->loc, 3576, "semantics in type overridden by variable/function or enclosing type");
            if (semantic)
                s = semantic;
            if (!s) {
                if (hl_type_has_object(f->type) || f->type->kind == TY_STRUCT) {
                    walk_type(l, list, f->type, nullptr, &offset, first, interp | f->interp, output, f->loc, f->name);
                    continue;
                }
                hl_error(l->h, f->loc, 3502, "'%s': %s parameter '%s' missing semantics", l->entry->name,
                         output ? "output" : "input", f->name);
                l->failed = true;
                *first += hl_type_components(f->type);
                continue;
            }
            uint32_t *io = semantic ? index_offset : &offset;
            walk_type(l, list, f->type, s, io, first, interp | f->interp, output, f->loc, f->name);
        }
        return;
    case TY_ARRAY:
        for (uint32_t i = 0; i < t->count; i++)
            walk_type(l, list, t->elem, semantic, index_offset, first, interp, output, loc, what);
        return;
    case TY_MATRIX: {
        HlType *row = hl_type_vector(l->h, t->base, t->cols);
        for (uint32_t r = 0; r < t->rows; r++) {
            add_element(l, list, semantic, (*index_offset)++, row, *first, interp, output, loc);
            *first += t->cols;
        }
        return;
    }
    case TY_SCALAR:
    case TY_VECTOR:
        if (!semantic) {
            hl_error(l->h, loc, 3502, "'%s': %s parameter '%s' missing semantics", l->entry->name,
                     output ? "output" : "input", what ? what : "");
            l->failed = true;
            *first += hl_type_components(t);
            return;
        }
        add_element(l, list, semantic, (*index_offset)++, t, *first, interp, output, loc);
        *first += hl_type_components(t);
        return;
    default:
        return;
    }
}

/* registers for the elements: first fit, in declaration order, with interpolation kept apart. */
static void pack(Lower *l, PendingList *list, IrSignature *sig, bool packed, bool targets)
{
    uint8_t used[64] = {};
    uint32_t interp_of[64] = {};
    for (int pass = 0; pass < 2; pass++)
        for (uint32_t i = 0; i < list->count; i++) {
            Pending *p = &list->items[i];
            if (p->generated != (pass == 1))
                continue;
            if (p->operand) {
                p->e.reg = ~0u;
                p->e.operand = p->operand;
                continue;
            }
            uint8_t width = (uint8_t)(32 - __builtin_clz(p->e.mask));
            if (targets && p->e.sysval == SV_TARGET) {
                p->e.reg = p->e.index;
                p->e.start = 0;
                used[p->e.reg & 63] |= p->e.mask;
                continue;
            }
            for (uint32_t r = 0; r < 64; r++) {
                if (!packed && used[r])
                    continue;
                if (used[r] && interp_of[r] != p->e.interp)
                    continue;
                /* system values that the rasterizer reads keep registers of their own. */
                if (used[r] && (p->e.sysval || (list->items[i].e.sysval)))
                    continue;
                bool fits = false;
                for (uint8_t start = 0; start + width <= 4; start++) {
                    uint8_t m = (uint8_t)(((1u << width) - 1) << start);
                    if (used[r] & m)
                        continue;
                    /* components fill from where the register's last element ended. */
                    if (used[r] && (1u << start) < (uint32_t)(used[r] & -used[r]))
                        continue;
                    p->e.reg = r;
                    p->e.start = start;
                    p->e.mask = m;
                    used[r] |= m;
                    interp_of[r] = p->e.interp;
                    fits = true;
                    break;
                }
                if (fits)
                    break;
            }
            /* a register holding a system value takes nothing else. */
            if (p->e.sysval && p->e.reg < 64)
                used[p->e.reg] = 0xF;
        }
    for (uint32_t i = 0; i < list->count; i++)
        HL_PUSH(l->h, sig->elements, sig->count, sig->capacity, list->items[i].e);
}

static Slot *slots_of(Lower *l, PendingList *list, uint32_t start, uint32_t components)
{
    Slot *slots = hl_alloc(l->h, (components ? components : 1) * sizeof(Slot));
    for (uint32_t i = start; i < list->count; i++) {
        Pending *p = &list->items[i];
        for (uint32_t c = 0; c < p->count; c++) {
            uint32_t at = p->first + c;
            if (at >= components)
                continue;
            if (p->operand) {
                slots[at] = (Slot){SLOT_SV, (uint8_t)c, 0, p->operand};
            } else {
                slots[at] = (Slot){SLOT_REG, (uint8_t)(p->e.start + c), p->e.reg, 0};
            }
        }
    }
    return slots;
}

static uint32_t attr_uint(Lower *l, const HlAttribute *a, uint32_t i, uint32_t fallback)
{
    if (i >= a->arg_count || a->args[i]->kind != E_CONST)
        return fallback;
    return a->args[i]->values[0].u;
}

static const HlAttribute *func_attr(const HlFunc *f, const char *name)
{
    for (uint32_t i = 0; i < f->attr_count; i++)
        if (!hl_stricmp(f->attrs[i].name, name))
            return &f->attrs[i];
    return nullptr;
}

/* the values of an input parameter, from its slots. */
static Value input_value(Lower *l, const HlType *t, const Slot *slots, uint32_t count)
{
    Value v = lw_value(l, (HlType *)t, count);
    for (uint32_t i = 0; i < count; i++) {
        const Slot *s = &slots[i];
        if (s->kind == SLOT_REG)
            v.c[i] = (IrSrc){.kind = SRC_IN, .v = s->reg, .comp = s->comp};
        else if (s->kind == SLOT_SV)
            v.c[i] = (IrSrc){.kind = SRC_SV, .v = s->operand, .comp = s->comp};
        else
            v.c[i] = ir_imm(0);
        uint8_t base = hl_type_component_base(t, i);
        if (hl_base_storage(base) == BT_BOOL && s->kind != SLOT_NONE) {
            /* SV_IsFrontFace is ~0 already; other bools come as 0 and 1. */
            v.c[i] = lw_op2(l, OP_INE, v.c[i], ir_imm(0));
        }
    }
    return v;
}

static void write_outputs(Lower *l, Value v, const Slot *slots)
{
    for (uint32_t i = 0; i < v.count; i++) {
        const Slot *s = &slots[i];
        if (s->kind == SLOT_NONE)
            continue;
        IrIns *ins = ir_emit(l->s, IR_STORE, 0, 1);
        if (s->kind == SLOT_SV)
            ins->store = (IrDst){.kind = DST_SPECIAL, .v = s->operand, .comp = 0};
        else
            ins->store = (IrDst){.kind = DST_OUT, .v = s->reg, .comp = s->comp};
        ins->src[0] = v.c[i];
    }
}

static Value var_value(Lower *l, VarInfo *vi)
{
    LValue lv = {.kind = vi->kind == VK_X ? LV_X : LV_REGS, .vi = vi, .type = vi->type, .count = vi->count};
    lv.comps = hl_alloc(l->h, (vi->count ? vi->count : 1) * sizeof(uint32_t));
    for (uint32_t i = 0; i < vi->count; i++)
        lv.comps[i] = i;
    return lw_load(l, &lv);
}

static void var_store(Lower *l, VarInfo *vi, Value v)
{
    LValue lv = {.kind = vi->kind == VK_X ? LV_X : LV_REGS, .vi = vi, .type = vi->type, .count = vi->count};
    lv.comps = hl_alloc(l->h, (vi->count ? vi->count : 1) * sizeof(uint32_t));
    for (uint32_t i = 0; i < vi->count; i++)
        lv.comps[i] = i;
    lw_store(l, &lv, v);
}


/* geometry shader output vertices go to the slots of the stream's type. */
void lw_stream_store(Lower *l, Value v)
{
    Slot *slots = l->stream_slots;
    if (!slots)
        return;
    write_outputs(l, v, slots);
}

static uint32_t gs_primitive(uint32_t storage)
{
    if (storage & ST_POINT)
        return 1;
    if (storage & ST_LINEADJ)
        return 6;
    if (storage & ST_TRIANGLEADJ)
        return 7;
    if (storage & ST_LINE)
        return 2;
    if (storage & ST_TRIANGLE)
        return 3;
    return 0;
}

int lw_entry(Lower *l)
{
    Hlsl *h = l->h;
    IrShader *s = l->s;
    HlFunc *f = l->entry;
    uint8_t stage = s->profile.stage;
    if (stage == STAGE_COMPUTE) {
        const HlAttribute *a = func_attr(f, "numthreads");
        if (!a) {
            hl_error(h, f->loc, 3532, "%s: compute shader must have a numthreads attribute", f->name);
            return -1;
        }
        for (uint32_t i = 0; i < 3; i++)
            s->thread_group[i] = attr_uint(l, a, i, 1);
        uint32_t total = s->thread_group[0] * s->thread_group[1] * s->thread_group[2];
        uint32_t max = s->profile.major >= 5 ? 1024 : 768;
        if (!total || total > max || s->thread_group[2] > 64 || s->thread_group[0] > 1024 || s->thread_group[1] > 1024) {
            hl_error(h, a->loc, 4520, "numthreads(%u,%u,%u) is out of range", s->thread_group[0], s->thread_group[1],
                     s->thread_group[2]);
            return -1;
        }
    }
    if (stage == STAGE_GEOMETRY) {
        const HlAttribute *a = func_attr(f, "maxvertexcount");
        if (!a) {
            hl_error(h, f->loc, 3539, "%s: geometry shader must have a maxvertexcount attribute", f->name);
            return -1;
        }
        s->gs_max_vertices = attr_uint(l, a, 0, 1);
        const HlAttribute *inst = func_attr(f, "instance");
        s->gs_instances = inst ? attr_uint(l, inst, 0, 1) : 0;
    }
    if (stage == STAGE_PIXEL && func_attr(f, "earlydepthstencil"))
        s->early_depth = true;
    if (stage == STAGE_HULL || stage == STAGE_DOMAIN) {
        hl_error(h, f->loc, 3000, "hull and domain shaders are not supported yet");
        return -1;
    }

    lw_globals(l);
    PendingList in = {}, out = {};
    uint32_t params = f->param_count;
    Slot **slots = hl_alloc(h, (params + 1) * sizeof(Slot *));
    uint32_t *starts = hl_alloc(h, (params + 1) * sizeof(uint32_t));
    uint32_t *out_starts = hl_alloc(h, (params + 1) * sizeof(uint32_t));
    /* inputs, in parameter order. */
    for (uint32_t i = 0; i < params; i++) {
        HlVar *p = f->params[i];
        starts[i] = in.count;
        if (!(p->storage & ST_IN) || (p->storage & ST_UNIFORM) || p->type->kind == TY_OBJECT)
            continue;
        uint32_t first = 0, offset = 0;
        const HlType *t = p->type;
        if (stage == STAGE_GEOMETRY && t->kind == TY_ARRAY) {
            s->gs_input_primitive = gs_primitive(p->storage);
            t = t->elem;
        }
        walk_type(l, &in, t, p->semantic, &offset, &first, p->interp, false, p->loc, p->name);
    }
    starts[params] = in.count;
    if (stage == STAGE_GEOMETRY && !s->gs_input_primitive) {
        hl_error(h, f->loc, 3000, "%s: geometry shader needs an input primitive", f->name);
        return -1;
    }
    /* outputs: the parameters, then the return value. */
    HlType *stream = nullptr;
    for (uint32_t i = 0; i < params; i++) {
        HlVar *p = f->params[i];
        out_starts[i] = out.count;
        if (p->type->kind == TY_OBJECT && (p->type->object >= OBJ_POINT_STREAM && p->type->object <= OBJ_TRIANGLE_STREAM)) {
            stream = p->type;
            s->gs_output_topology = p->type->object == OBJ_POINT_STREAM ? 1 : p->type->object == OBJ_LINE_STREAM ? 3 : 5;
            uint32_t first = 0, offset = 0;
            walk_type(l, &out, p->type->elem, nullptr, &offset, &first, 0, true, p->loc, p->name);
            continue;
        }
        if (!(p->storage & ST_OUT) || p->type->kind == TY_OBJECT)
            continue;
        uint32_t first = 0, offset = 0;
        walk_type(l, &out, p->type, p->semantic, &offset, &first, p->interp, true, p->loc, p->name);
    }
    out_starts[params] = out.count;
    if (f->ret->kind != TY_VOID) {
        uint32_t first = 0, offset = 0;
        walk_type(l, &out, f->ret, f->semantic, &offset, &first, f->ret_interp, true, f->loc, f->name);
    }
    if (l->failed || h->errors)
        return -1;
    bool pack_inputs = stage != STAGE_VERTEX;
    pack(l, &in, &s->inputs, pack_inputs, false);
    pack(l, &out, &s->outputs, stage != STAGE_PIXEL, stage == STAGE_PIXEL);
    if (stage == STAGE_COMPUTE) {
        s->inputs.count = 0;
        s->outputs.count = 0;
    }
    for (uint32_t i = 0; i < in.count; i++)
        if (in.items[i].operand)
            s->sv_mask[in.items[i].operand & 63] |= in.items[i].e.mask;

    /* the body, inlined like any call, with inputs as known values. */
    lw_statics(l);
    VarInfo **param_info = hl_alloc(h, (params + 1) * sizeof(VarInfo *));
    Slot **out_slots = hl_alloc(h, (params + 1) * sizeof(Slot *));
    for (uint32_t i = 0; i < params; i++) {
        HlVar *p = f->params[i];
        if (p->type->kind == TY_OBJECT) {
            VarInfo *vi = hl_alloc(h, sizeof(VarInfo));
            vi->kind = VK_OBJECT;
            vi->var = p;
            vi->type = p->type;
            p->lower = vi;
            if (stream)
                l->stream_slots = slots_of(l, &out, out_starts[i], hl_type_components(p->type->elem));
            continue;
        }
        if (stage == STAGE_GEOMETRY && p->type->kind == TY_ARRAY && (p->storage & ST_IN)) {
            /* vertices of the input primitive: inputs read at a vertex index. */
            const HlType *elem = p->type->elem;
            uint32_t n = hl_type_components(elem);
            Slot *sl = slots_of(l, &in, starts[i], n);
            VarInfo *vi = hl_alloc(h, sizeof(VarInfo));
            vi->kind = VK_VERTICES;
            vi->var = p;
            vi->type = p->type;
            vi->count = hl_type_components(p->type);
            vi->vertex_components = n;
            vi->in_reg = hl_alloc(h, (n ? n : 1) * sizeof(uint32_t));
            vi->in_comp = hl_alloc(h, (n ? n : 1) * sizeof(uint8_t));
            for (uint32_t c = 0; c < n; c++) {
                vi->in_reg[c] = sl[c].reg;
                vi->in_comp[c] = sl[c].comp;
            }
            p->lower = vi;
            s->input_control_points = p->type->count;
            continue;
        }
        VarInfo *vi = lw_new_local(l, p, p->type);
        p->lower = vi;
        param_info[i] = vi;
        uint32_t n = hl_type_components(p->type);
        if ((p->storage & ST_IN) && !(p->storage & ST_UNIFORM)) {
            slots[i] = slots_of(l, &in, starts[i], n);
            var_store(l, vi, input_value(l, p->type, slots[i], n));
        } else {
            Value zero = lw_value(l, p->type, n);
            for (uint32_t c = 0; c < n; c++)
                zero.c[c] = ir_imm(0);
            var_store(l, vi, zero);
        }
        if (p->storage & ST_OUT)
            out_slots[i] = slots_of(l, &out, out_starts[i], n);
    }
    Value ret = lw_entry_body(l, f);
    for (uint32_t i = 0; i < params; i++) {
        HlVar *p = f->params[i];
        if (!(p->storage & ST_OUT) || p->type->kind == TY_OBJECT || !param_info[i])
            continue;
        write_outputs(l, var_value(l, param_info[i]), out_slots[i]);
    }
    if (f->ret->kind != TY_VOID) {
        Slot *rs = slots_of(l, &out, out_starts[params], hl_type_components(f->ret));
        write_outputs(l, ret, rs);
    }
    ir_emit(s, OP_RET, 0, 0);
    return l->failed || h->errors ? -1 : 0;
}

int ir_lower(IrShader *s, HlFunc *entry)
{
    Lower l_ = {.s = s, .h = s->h, .prog = s->prog, .entry = entry}, *l = &l_;
    return lw_entry(l);
}
