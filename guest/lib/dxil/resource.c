/*
 * resources: the handles dxil makes of the root signature's ranges or of
 * the descriptor heaps, and the operations that read and write through
 * them. the binding model is dxbc's for direct3d 12, see dxbc.h: views and
 * samplers of the heaps by their index in the mutable sets, constants and
 * root descriptors in push constants, and raw, structured and constant
 * buffer views of a table through the device address of their record.
 */
#include "internal.h"

enum { BIND_NONE = 0xFF };

static uint32_t t_i32(Compiler *c) { return spv_type_int(&c->b, 32, 1); }

/* ------------------------------------------------------------------ metadata and handles */

static void read_tags(Compiler *c, const DxMetadata *tags, ResourceInfo *r)
{
    if (!tags)
        return;
    for (uint32_t i = 0; i + 1 < tags->count; i += 2) {
        uint64_t tag = dx_md_int(&c->m, tags, i, ~0ull), value = dx_md_int(&c->m, tags, i + 1, 0);
        if (tag == 0)
            r->comp = (uint8_t)value;
        else if (tag == 1)
            r->stride = (uint32_t)value;
    }
}

int dxil_read_resources(Compiler *c, const DxMetadata *list)
{
    if (!list)
        return 0;
    for (uint32_t cls = 0; cls < 4; cls++) {
        const DxMetadata *ranges = dx_md_op(&c->m, list, cls);
        if (!ranges)
            continue;
        uint32_t count = 0;
        for (uint32_t i = 0; i < ranges->count; i++) {
            uint32_t id = (uint32_t)dx_md_int(&c->m, dx_md_op(&c->m, ranges, i), 0, i);
            if (id + 1 > count)
                count = id + 1;
        }
        c->ranges[cls] = dxil_alloc(c, (count + 1) * sizeof(ResourceInfo));
        c->range_count[cls] = count;
        if (!c->ranges[cls])
            return -1;
        for (uint32_t i = 0; i < ranges->count; i++) {
            const DxMetadata *md = dx_md_op(&c->m, ranges, i);
            if (!md)
                continue;
            ResourceInfo *r = &c->ranges[cls][dx_md_int(&c->m, md, 0, i)];
            r->cls = (uint8_t)cls;
            /* shader model 6.6 libraries cast their globals of handles to the resource's type. */
            const DxMetadata *g = dx_md_op(&c->m, md, 1);
            uint32_t gid = g && g->kind == DM_VALUE ? g->value : DX_NONE;
            const DxValue *gv = gid != DX_NONE ? dx_value(&c->m, NULL, gid) : NULL;
            if (gv && gv->kind == DV_EXPR && gv->sub == DX_EXPR_CAST && gv->count) {
                gid = gv->ops[0];
                gv = dx_value(&c->m, NULL, gid);
            }
            if (gv && gv->kind == DV_GLOBAL)
                r->global = gid + 1;
            r->space = (uint32_t)dx_md_int(&c->m, md, 3, 0);
            r->lower = (uint32_t)dx_md_int(&c->m, md, 4, 0);
            r->size = (uint32_t)dx_md_int(&c->m, md, 5, 1);
            r->comp = COMP_F32;
            r->components = 4;
            switch (cls) {
            case CLASS_SRV:
                r->kind = (uint8_t)dx_md_int(&c->m, md, 6, RK_TEXTURE2D);
                r->samples = (uint32_t)dx_md_int(&c->m, md, 7, 0);
                read_tags(c, dx_md_op(&c->m, md, 8), r);
                break;
            case CLASS_UAV:
                r->kind = (uint8_t)dx_md_int(&c->m, md, 6, RK_TEXTURE2D);
                r->coherent = (uint8_t)dx_md_int(&c->m, md, 7, 0);
                r->counter = (uint8_t)dx_md_int(&c->m, md, 8, 0);
                r->rov = (uint8_t)dx_md_int(&c->m, md, 9, 0);
                read_tags(c, dx_md_op(&c->m, md, 10), r);
                break;
            case CLASS_CBV:
                r->kind = RK_CBUFFER;
                r->stride = (uint32_t)dx_md_int(&c->m, md, 6, 0);
                break;
            default:
                r->kind = RK_SAMPLER;
                r->comparison = dx_md_int(&c->m, md, 6, 0) == 1;
                break;
            }
        }
    }
    return 0;
}

static uint32_t new_handle(Compiler *c, const Handle *h)
{
    if (c->handle_count == c->handle_capacity) {
        uint32_t want = c->handle_capacity ? c->handle_capacity * 2 : 32;
        Handle *list = dxil_alloc(c, want * sizeof(Handle));
        if (!list)
            return 0;
        if (c->handles)
            __builtin_memcpy(list, c->handles, c->handle_count * sizeof(Handle));
        c->handles = list;
        c->handle_capacity = want;
    }
    c->handles[c->handle_count] = *h;
    return c->handle_count++;
}

static void set_handle(Compiler *c, uint32_t id, const Handle *h)
{
    Value *s = dxil_slot(c, id);
    if (!s)
        return;
    s->kind = VAL_HANDLE;
    s->handle = new_handle(c, h);
}

static Handle *handle_of(Compiler *c, uint32_t id)
{
    Value *s = dxil_slot(c, id);
    if (!s || s->kind != VAL_HANDLE || s->handle >= c->handle_count) {
        dxil_fail(c, "a resource operation without its handle");
        return NULL;
    }
    return &c->handles[s->handle];
}

/* the integers of a constant structure, the operands of createHandleFromBinding and annotateHandle. */
static void constant_fields(Compiler *c, uint32_t id, uint64_t *out, uint32_t count)
{
    const DxValue *v = dx_value(&c->m, c->f, id);
    for (uint32_t i = 0; i < count; i++)
        out[i] = 0;
    if (!v || v->kind != DV_AGGREGATE)
        return;
    for (uint32_t i = 0; i < count && i < v->count; i++)
        out[i] = v->data ? v->data[i] : dxil_constant(c, v->ops[i], 0);
}

static int create_handle(Compiler *c, const DxInst *ins)
{
    uint32_t cls = (uint32_t)dxil_constant(c, ins->ops[2], 0), range = (uint32_t)dxil_constant(c, ins->ops[3], 0);
    if (cls > 3 || range >= c->range_count[cls])
        return dxil_fail(c, "a handle of a range that does not exist");
    Handle h = {c->ranges[cls][range], ins->ops[4], 0, (uint8_t)(dxil_constant(c, ins->ops[5], 0) & 1), 1};
    set_handle(c, ins->value, &h);
    return 0;
}

static int create_from_binding(Compiler *c, const DxInst *ins)
{
    uint64_t bind[4];
    constant_fields(c, ins->ops[2], bind, 4);
    Handle h = {{0}, ins->ops[3], 0, (uint8_t)(dxil_constant(c, ins->ops[4], 0) & 1), 0};
    h.r.lower = (uint32_t)bind[0];
    h.r.size = (uint32_t)(bind[1] - bind[0] + 1);
    h.r.space = (uint32_t)bind[2];
    h.r.cls = (uint8_t)bind[3];
    set_handle(c, ins->value, &h);
    return 0;
}

/* the register of an element of an array of resources: the first's plus the flat index of a gep's operands. */
static uint32_t element_register(Compiler *c, uint32_t reg, const uint32_t *ops, uint32_t count)
{
    const DxValue *base = dx_value(&c->m, c->f, ops[0]);
    const DxType *t = base ? &c->m.types[c->m.types[base->type].element] : NULL;
    for (uint32_t i = 2; t && t->kind == DT_ARRAY && i < count; i++) {
        uint32_t below = 1;
        for (const DxType *e = &c->m.types[t->element]; e->kind == DT_ARRAY; e = &c->m.types[e->element])
            below *= e->count;
        uint32_t k = spv_op2(&c->b, SpvOpIMul, c->t_u32, dxil_value(c, ops[i]), dxil_uconst(c, below));
        reg = spv_op2(&c->b, SpvOpIAdd, c->t_u32, reg, k);
        t = &c->m.types[t->element];
    }
    return reg;
}

/* the handle a pointer to a resource's global, or to an element of its array, stands for; 0 when it is none. */
static int resource_pointer(Compiler *c, uint32_t id, Handle *out)
{
    const DxValue *v = dx_value(&c->m, c->f, id);
    if (!v)
        return 0;
    if (v->kind == DV_GLOBAL) {
        for (uint32_t cls = 0; cls < 4; cls++)
            for (uint32_t i = 0; i < c->range_count[cls]; i++)
                if (c->ranges[cls][i].global == id + 1) {
                    __builtin_memset(out, 0, sizeof(*out));
                    out->r = c->ranges[cls][i];
                    out->index = DX_NONE;
                    out->spv_register = dxil_uconst(c, out->r.lower);
                    return 1;
                }
        return 0;
    }
    if (v->kind == DV_EXPR && v->sub == DX_EXPR_GEP && v->count >= 2) {
        if (!resource_pointer(c, v->ops[0], out))
            return 0;
        out->spv_register = element_register(c, out->spv_register, v->ops, v->count);
        return 1;
    }
    Value *s = dxil_slot(c, id);
    if (v->kind == DV_INSTRUCTION && s && s->kind == VAL_HANDLE && s->handle < c->handle_count) {
        *out = c->handles[s->handle];
        return 1;
    }
    return 0;
}

int dxil_resource_global(Compiler *c, const DxInst *ins)
{
    Handle h;
    if (!c->options->export_name || !resource_pointer(c, ins->ops[0], &h))
        return 0;
    if (ins->op == DI_GEP)
        h.spv_register = element_register(c, h.spv_register, ins->ops, ins->count);
    set_handle(c, ins->value, &h);
    return 1;
}

static int create_for_lib(Compiler *c, const DxInst *ins)
{
    Value *s = dxil_slot(c, ins->ops[2]);
    if (!s || s->kind != VAL_HANDLE || s->handle >= c->handle_count)
        return dxil_fail(c, "a library handle of something not a resource");
    Handle h = c->handles[s->handle];
    set_handle(c, ins->value, &h);
    return 0;
}

static int create_from_heap(Compiler *c, const DxInst *ins)
{
    Handle h = {{0}, ins->ops[2], 1, (uint8_t)(dxil_constant(c, ins->ops[4], 0) & 1), 0};
    h.r.cls = dxil_constant(c, ins->ops[3], 0) & 1 ? CLASS_SAMPLER : CLASS_SRV;
    set_handle(c, ins->value, &h);
    return 0;
}

/*
 * the properties annotateHandle gives, as dxc writes them: the kind in the
 * low byte of the first word, then bit 12 for views of unordered access,
 * 13 rasterizer ordered, 14 globally coherent and 15 a counter, or a
 * comparison for samplers. the second word holds a typed view's component
 * type and count in its two low bytes, or a structure's stride, or a
 * constant buffer's size.
 */
static int annotate(Compiler *c, const DxInst *ins)
{
    Handle *from = handle_of(c, ins->ops[2]);
    if (!from)
        return -1;
    Handle h = *from;
    uint64_t props[2];
    constant_fields(c, ins->ops[3], props, 2);
    uint32_t w0 = (uint32_t)props[0], w1 = (uint32_t)props[1];
    h.r.kind = (uint8_t)w0;
    int uav = (w0 >> 12) & 1;
    h.r.rov = (w0 >> 13) & 1;
    h.r.coherent = (w0 >> 14) & 1;
    h.r.comp = COMP_F32;
    h.r.components = 4;
    switch (h.r.kind) {
    case RK_SAMPLER:
        h.r.cls = CLASS_SAMPLER;
        h.r.comparison = (w0 >> 15) & 1;
        break;
    case RK_CBUFFER:
        h.r.cls = CLASS_CBV;
        h.r.stride = w1;
        break;
    case RK_RAW_BUFFER: h.r.cls = uav ? CLASS_UAV : CLASS_SRV; break;
    case RK_STRUCTURED_BUFFER:
    case RK_TBUFFER:
        h.r.cls = uav ? CLASS_UAV : CLASS_SRV;
        h.r.stride = w1;
        break;
    default:
        h.r.cls = uav ? CLASS_UAV : CLASS_SRV;
        h.r.comp = (uint8_t)w1;
        h.r.components = (uint8_t)(w1 >> 8);
        h.r.samples = (w1 >> 16) & 0xFF;
        break;
    }
    if (uav)
        h.r.counter = (w0 >> 15) & 1;
    h.annotated = 1;
    set_handle(c, ins->value, &h);
    return 0;
}

/* ------------------------------------------------------------------ the root signature */

static const DxbcRootBinding *root_binding(Compiler *c, uint32_t cls, uint32_t space, uint32_t reg)
{
    static const uint8_t TYPES[4] = {DXBC_BIND_SRV, DXBC_BIND_UAV, DXBC_BIND_CBV, DXBC_BIND_SAMPLER};
    const DxbcCompileOptions *o = c->options;
    for (uint32_t i = 0; i < o->root_count; i++) {
        const DxbcRootBinding *b = &o->root[i];
        if (b->type == TYPES[cls & 3] && b->space == space && reg >= b->base &&
            (b->count == ~0u || reg - b->base < b->count))
            return b;
    }
    dxil_fail(c, "a register outside the root signature");
    return NULL;
}

/* a dword of the push constants, the offset a uint value in bytes. */
static uint32_t push_word(Compiler *c, uint32_t byte_offset)
{
    if (!c->push) {
        uint32_t array = spv_type_array_unique(&c->b, c->t_u32, 64);
        dxil_decorate(c, array, SpvDecorationArrayStride, 4);
        uint32_t block = spv_type_struct_unique(&c->b, &array, 1);
        uint32_t offset = 0;
        spv_member_decorate(&c->b, block, 0, SpvDecorationOffset, &offset, 1);
        spv_decorate(&c->b, block, SpvDecorationBlock, NULL, 0);
        c->push = dxil_global(c, SpvStorageClassPushConstant, block, 0);
    }
    /* reads past the 64 dwords are clamped, as vulkan leaves them undefined. */
    uint32_t args[2] = {spv_op2(&c->b, SpvOpShiftRightLogical, c->t_u32, byte_offset, dxil_uconst(c, 2)),
                        dxil_uconst(c, 63)};
    uint32_t w[3] = {c->push, dxil_uconst(c, 0), spv_glsl(&c->b, c->t_u32, GLSLstd450UMin, args, 2)};
    uint32_t ptr = spv_op(&c->b, SpvOpAccessChain, spv_type_pointer(&c->b, SpvStorageClassPushConstant, c->t_u32), w, 3);
    return spv_load(&c->b, c->t_u32, ptr);
}

/* a dword of the shader record, which holds the arguments of local root signatures. */
static uint32_t record_word(Compiler *c, uint32_t byte_offset)
{
    if (!c->record) {
        /* direct3d's records are 4096 bytes at most. */
        uint32_t array = spv_type_array_unique(&c->b, c->t_u32, 1024);
        dxil_decorate(c, array, SpvDecorationArrayStride, 4);
        uint32_t block = spv_type_struct_unique(&c->b, &array, 1);
        uint32_t offset = 0;
        spv_member_decorate(&c->b, block, 0, SpvDecorationOffset, &offset, 1);
        spv_member_decorate(&c->b, block, 0, SpvDecorationNonWritable, NULL, 0);
        spv_decorate(&c->b, block, SpvDecorationBlock, NULL, 0);
        c->record = dxil_global(c, SpvStorageClassShaderRecordBufferKHR, block, 0);
    }
    uint32_t w[3] = {c->record, dxil_uconst(c, 0),
                     spv_op2(&c->b, SpvOpShiftRightLogical, c->t_u32, byte_offset, dxil_uconst(c, 2))};
    uint32_t ptr =
        spv_op(&c->b, SpvOpAccessChain, spv_type_pointer(&c->b, SpvStorageClassShaderRecordBufferKHR, c->t_u32), w, 3);
    return spv_load(&c->b, c->t_u32, ptr);
}

/* a dword of a root argument: in the push constants, or the shader record for local root signatures. */
static uint32_t root_word(Compiler *c, const DxbcRootBinding *b, uint32_t byte_offset)
{
    return b->record ? record_word(c, byte_offset) : push_word(c, byte_offset);
}

/* the heap index of a table's first descriptor: a push constant, or a descriptor handle in the shader record. */
static uint32_t table_start(Compiler *c, const DxbcRootBinding *b)
{
    if (!b->record)
        return push_word(c, dxil_uconst(c, b->push_offset));
    uint32_t bits = c->options->handle_bits, shift = c->options->increment_shift;
    uint32_t lo = record_word(c, dxil_uconst(c, b->push_offset));
    if (bits < 32)
        lo = spv_op2(&c->b, SpvOpBitwiseAnd, c->t_u32, lo, dxil_uconst(c, (1u << bits) - 1));
    uint32_t index = spv_op2(&c->b, SpvOpShiftRightLogical, c->t_u32, lo, dxil_uconst(c, shift));
    if (bits > 32 && shift) {
        uint32_t hi = record_word(c, dxil_uconst(c, b->push_offset + 4));
        hi = spv_op2(&c->b, SpvOpBitwiseAnd, c->t_u32, hi, dxil_uconst(c, (1u << (bits - 32)) - 1));
        hi = spv_op2(&c->b, SpvOpShiftLeftLogical, c->t_u32, hi, dxil_uconst(c, 32 - shift));
        index = spv_op2(&c->b, SpvOpBitwiseOr, c->t_u32, index, hi);
    }
    return index;
}

/* the register a handle names, as a u32. */
static uint32_t handle_register(Compiler *c, const Handle *h)
{
    if (h->spv_register)
        return h->spv_register;
    const DxValue *v = dx_value(&c->m, c->f, h->index);
    if (!v || v->kind == DV_UNDEF)
        return dxil_uconst(c, h->r.lower);
    return dxil_value(c, h->index);
}

/* the root binding a handle of a range is in, or NULL for heap handles. */
static const DxbcRootBinding *binding_of(Compiler *c, const Handle *h)
{
    if (h->heap)
        return NULL;
    return root_binding(c, h->r.cls, h->r.space, h->r.lower);
}

static uint32_t nonuniform(Compiler *c, const Handle *h, uint32_t id)
{
    if (h->nonuniform) {
        spv_capability(&c->b, SpvCapabilityShaderNonUniform);
        spv_decorate(&c->b, id, SpvDecorationNonUniform, NULL, 0);
    }
    return id;
}

/* the index of a handle's descriptor in its heap: the heap index itself, or the table's start and place. */
static uint32_t heap_index(Compiler *c, const Handle *h, const DxbcRootBinding *b)
{
    if (h->heap)
        return nonuniform(c, h, handle_register(c, h));
    if (!b)
        return dxil_uconst(c, 0);
    uint32_t reg = handle_register(c, h);
    uint32_t from_base = spv_op2(&c->b, SpvOpISub, c->t_u32, reg, dxil_uconst(c, b->base));
    uint32_t start = b->source == DXBC_SOURCE_TABLE ? table_start(c, b) : dxil_uconst(c, 0);
    uint32_t base = spv_op2(&c->b, SpvOpIAdd, c->t_u32, start, dxil_uconst(c, b->offset));
    return nonuniform(c, h, spv_op2(&c->b, SpvOpIAdd, c->t_u32, base, from_base));
}

static void nonuniform_capability(Compiler *c, uint32_t storage_image, int texel_buffer)
{
    static const uint32_t CAPS[2][2] = {
        {SpvCapabilitySampledImageArrayNonUniformIndexing, SpvCapabilityStorageImageArrayNonUniformIndexing},
        {SpvCapabilityUniformTexelBufferArrayNonUniformIndexing, SpvCapabilityStorageTexelBufferArrayNonUniformIndexing}};
    spv_capability(&c->b, CAPS[texel_buffer][storage_image]);
}

/* the shape of an image of a resource kind. */
static void image_shape(uint32_t kind, uint32_t *dim, uint32_t *arrayed, uint32_t *ms)
{
    *arrayed = *ms = 0;
    switch (kind) {
    case RK_TEXTURE1D: *dim = SpvDim1D; break;
    case RK_TEXTURE1DARRAY: *dim = SpvDim1D, *arrayed = 1; break;
    case RK_TEXTURE2DMS: *dim = SpvDim2D, *ms = 1; break;
    case RK_TEXTURE2DMSARRAY: *dim = SpvDim2D, *ms = 1, *arrayed = 1; break;
    case RK_TEXTURE2DARRAY: *dim = SpvDim2D, *arrayed = 1; break;
    case RK_TEXTURE3D: *dim = SpvDim3D; break;
    case RK_TEXTURECUBE: *dim = SpvDimCube; break;
    case RK_TEXTURECUBEARRAY: *dim = SpvDimCube, *arrayed = 1; break;
    case RK_TYPED_BUFFER:
    case RK_RAW_BUFFER:
    case RK_STRUCTURED_BUFFER: *dim = SpvDimBuffer; break;
    default: *dim = SpvDim2D; break;
    }
}

/* the type images of a component type sample to: floats, signed or unsigned integers. */
static uint32_t sampled_type(Compiler *c, uint8_t comp)
{
    switch (comp) {
    case COMP_I16:
    case COMP_I32:
    case COMP_I64: return t_i32(c);
    case COMP_U16:
    case COMP_U32:
    case COMP_U64:
    case COMP_I1: return c->t_u32;
    default: return c->t_f32;
    }
}

/* the image type of a handle; format R32 for atomics, unknown otherwise. */
static uint32_t image_type(Compiler *c, const Handle *h, int depth, int atomic)
{
    uint32_t dim, arrayed, ms;
    image_shape(h->r.kind, &dim, &arrayed, &ms);
    int storage = h->r.cls == CLASS_UAV;
    if (dim == SpvDim1D)
        spv_capability(&c->b, storage ? SpvCapabilityImage1D : SpvCapabilitySampled1D);
    if (dim == SpvDimBuffer)
        spv_capability(&c->b, storage ? SpvCapabilityImageBuffer : SpvCapabilitySampledBuffer);
    if (dim == SpvDimCube && arrayed)
        spv_capability(&c->b, storage ? SpvCapabilityImageCubeArray : SpvCapabilitySampledCubeArray);
    if (ms && arrayed && storage)
        spv_capability(&c->b, SpvCapabilityImageMSArray);
    if (ms && storage)
        spv_capability(&c->b, SpvCapabilityStorageImageMultisample);
    uint32_t type = sampled_type(c, h->r.comp), format = SpvImageFormatUnknown;
    if (storage) {
        if (atomic) {
            format = type == c->t_u32 ? SpvImageFormatR32ui : type == c->t_f32 ? SpvImageFormatR32f : SpvImageFormatR32i;
        } else {
            spv_capability(&c->b, SpvCapabilityStorageImageReadWithoutFormat);
            spv_capability(&c->b, SpvCapabilityStorageImageWriteWithoutFormat);
        }
    }
    return spv_type_image(&c->b, type, dim, depth ? 1 : 0, arrayed, ms, storage ? 2 : 1, format);
}

/* the resource set's variable for an image type, all of them on its one mutable binding. */
static uint32_t image_heap(Compiler *c, uint32_t type)
{
    for (uint32_t i = 0; i < c->image_count; i++)
        if (c->images[i].type == type)
            return c->images[i].var;
    uint32_t var = dxil_global(c, SpvStorageClassUniformConstant, spv_type_runtime_array(&c->b, type), 0);
    dxil_decorate(c, var, SpvDecorationDescriptorSet, DXBC_ROOT_SET_RESOURCES);
    dxil_decorate(c, var, SpvDecorationBinding, 0);
    if (c->image_count < 64) {
        c->images[c->image_count].type = type;
        c->images[c->image_count].var = var;
        c->image_count++;
    }
    return var;
}

/* the image type of a typed view's 64-bit texels, for 64-bit atomics. */
static uint32_t wide_image_type(Compiler *c, const Handle *h, uint32_t scalar, int is_signed)
{
    uint32_t dim, arrayed, ms;
    image_shape(h->r.kind, &dim, &arrayed, &ms);
    spv_capability(&c->b, SpvCapabilityInt64ImageEXT);
    spv_capability(&c->b, SpvCapabilityInt64Atomics);
    spv_extension(&c->b, "SPV_EXT_shader_image_int64");
    if (dim == SpvDimBuffer)
        spv_capability(&c->b, SpvCapabilityImageBuffer);
    if (dim == SpvDim1D)
        spv_capability(&c->b, SpvCapabilityImage1D);
    return spv_type_image(&c->b, scalar, dim, 0, arrayed, ms, 2, is_signed ? SpvImageFormatR64i : SpvImageFormatR64ui);
}

/* a pointer to a handle's view of 64-bit texels, in the set of them. */
static uint32_t wide_image_pointer(Compiler *c, const Handle *h, uint32_t type)
{
    uint32_t var = 0;
    for (uint32_t i = 0; i < c->wide_image_count; i++)
        if (c->wide_images[i].type == type)
            var = c->wide_images[i].var;
    if (!var) {
        var = dxil_global(c, SpvStorageClassUniformConstant, spv_type_runtime_array(&c->b, type), 0);
        dxil_decorate(c, var, SpvDecorationDescriptorSet, DXBC_ROOT_SET_ATOMICS);
        dxil_decorate(c, var, SpvDecorationBinding, 0);
        if (c->wide_image_count < 8) {
            c->wide_images[c->wide_image_count].type = type;
            c->wide_images[c->wide_image_count++].var = var;
        }
    }
    uint32_t w[2] = {var, heap_index(c, h, binding_of(c, h))};
    return nonuniform(c, h, spv_op(&c->b, SpvOpAccessChain, spv_type_pointer(&c->b, SpvStorageClassUniformConstant, type),
                                   w, 2));
}

/* a pointer to the image descriptor of a handle. */
static uint32_t image_pointer(Compiler *c, const Handle *h, uint32_t type)
{
    if (h->r.kind == RK_CBUFFER || h->r.kind == RK_SAMPLER) {
        dxil_fail(c, "an image operation on a buffer or sampler");
        return 0;
    }
    uint32_t index = heap_index(c, h, binding_of(c, h));
    if (h->nonuniform) {
        uint32_t dim, arrayed, ms;
        image_shape(h->r.kind, &dim, &arrayed, &ms);
        nonuniform_capability(c, h->r.cls == CLASS_UAV, dim == SpvDimBuffer);
    }
    uint32_t w[2] = {image_heap(c, type), index};
    return nonuniform(c, h, spv_op(&c->b, SpvOpAccessChain, spv_type_pointer(&c->b, SpvStorageClassUniformConstant, type),
                                   w, 2));
}

static uint32_t load_image(Compiler *c, const Handle *h, uint32_t type)
{
    uint32_t ptr = image_pointer(c, h, type);
    return ptr ? nonuniform(c, h, spv_load(&c->b, type, ptr)) : 0;
}

/* a sampler of the heap, or of the root signature's static samplers. */
static uint32_t load_sampler(Compiler *c, const Handle *h)
{
    uint32_t type = spv_type_sampler(&c->b);
    uint32_t ptr_type = spv_type_pointer(&c->b, SpvStorageClassUniformConstant, type);
    const DxbcRootBinding *b = binding_of(c, h);
    uint32_t index, var;
    if (b && b->source == DXBC_SOURCE_STATIC_SAMPLER) {
        if (!c->static_samplers) {
            c->static_samplers =
                dxil_global(c, SpvStorageClassUniformConstant, spv_type_runtime_array(&c->b, type), 0);
            dxil_decorate(c, c->static_samplers, SpvDecorationDescriptorSet, DXBC_ROOT_SET_STATIC_SAMPLERS);
            dxil_decorate(c, c->static_samplers, SpvDecorationBinding, 0);
        }
        uint32_t from_base = spv_op2(&c->b, SpvOpISub, c->t_u32, handle_register(c, h), dxil_uconst(c, b->base));
        index = spv_op2(&c->b, SpvOpIAdd, c->t_u32, dxil_uconst(c, b->offset), from_base);
        var = c->static_samplers;
    } else {
        if (!c->sampler_heap) {
            c->sampler_heap = dxil_global(c, SpvStorageClassUniformConstant, spv_type_runtime_array(&c->b, type), 0);
            dxil_decorate(c, c->sampler_heap, SpvDecorationDescriptorSet, DXBC_ROOT_SET_SAMPLERS);
            dxil_decorate(c, c->sampler_heap, SpvDecorationBinding, 0);
        }
        index = heap_index(c, h, b);
        var = c->sampler_heap;
    }
    if (h->nonuniform)
        spv_capability(&c->b, SpvCapabilitySampledImageArrayNonUniformIndexing);
    uint32_t w[2] = {var, index};
    uint32_t ptr = nonuniform(c, h, spv_op(&c->b, SpvOpAccessChain, ptr_type, w, 2));
    return nonuniform(c, h, spv_load(&c->b, type, ptr));
}

/* a device address, a uvec2, as a pointer of the type into physical storage. */
static uint32_t address_pointer(Compiler *c, uint32_t address, uint32_t pointer_type)
{
    spv_capability(&c->b, SpvCapabilityPhysicalStorageBufferAddresses);
    c->b.addressing = SpvAddressingModelPhysicalStorageBuffer64;
    return spv_op1(&c->b, SpvOpBitcast, pointer_type, address);
}

static uint32_t root_address(Compiler *c, const DxbcRootBinding *b)
{
    uint32_t parts[2] = {root_word(c, b, dxil_uconst(c, b->push_offset)),
                         root_word(c, b, dxil_uconst(c, b->push_offset + 4))};
    return dxil_vector(c, c->t_u32, parts, 2);
}

/* half of the record of a heap descriptor: address, size and stride, then the counter's address. */
static uint32_t heap_record(Compiler *c, uint32_t index, uint32_t half)
{
    uint32_t v4 = c->t_uvec[4];
    if (!c->records) {
        uint32_t array = spv_type_runtime_array(&c->b, v4);
        dxil_decorate(c, array, SpvDecorationArrayStride, 16);
        uint32_t block = spv_type_struct(&c->b, &array, 1);
        uint32_t offset = 0;
        spv_member_decorate(&c->b, block, 0, SpvDecorationOffset, &offset, 1);
        spv_member_decorate(&c->b, block, 0, SpvDecorationNonWritable, NULL, 0);
        spv_decorate(&c->b, block, SpvDecorationBlock, NULL, 0);
        c->records = dxil_global(c, SpvStorageClassStorageBuffer, block, 0);
        dxil_decorate(c, c->records, SpvDecorationDescriptorSet, DXBC_ROOT_SET_RECORDS);
        dxil_decorate(c, c->records, SpvDecorationBinding, 0);
    }
    uint32_t at = spv_op2(&c->b, SpvOpIAdd, c->t_u32,
                          spv_op2(&c->b, SpvOpShiftLeftLogical, c->t_u32, index, dxil_uconst(c, 1)), dxil_uconst(c, half));
    uint32_t w[3] = {c->records, dxil_uconst(c, 0), at};
    uint32_t ptr = spv_op(&c->b, SpvOpAccessChain, spv_type_pointer(&c->b, SpvStorageClassStorageBuffer, v4), w, 3);
    return spv_load(&c->b, v4, ptr);
}

static uint32_t uvec2_of(Compiler *c, uint32_t v, uint32_t first)
{
    uint32_t w[4] = {v, v, first, first + 1};
    return spv_op(&c->b, SpvOpVectorShuffle, c->t_uvec[2], w, 4);
}

/* the pointer type of an array of words or vec4s at a device address. */
static uint32_t root_array_pointer(Compiler *c, uint32_t element, uint32_t stride)
{
    uint32_t *cached = stride == 16 ? &c->root_vec4_pointer : stride == 8 ? &c->root_qword_pointer : &c->root_word_pointer;
    if (*cached)
        return *cached;
    uint32_t array = spv_type_runtime_array_unique(&c->b, element);
    dxil_decorate(c, array, SpvDecorationArrayStride, stride);
    uint32_t block = spv_type_struct_unique(&c->b, &array, 1);
    uint32_t offset = 0;
    spv_member_decorate(&c->b, block, 0, SpvDecorationOffset, &offset, 1);
    spv_decorate(&c->b, block, SpvDecorationBlock, NULL, 0);
    return *cached = spv_type_pointer(&c->b, SpvStorageClassPhysicalStorageBuffer, block);
}

/* a raw, structured or constant buffer read through its device address, with its size when it has a record. */
typedef struct Memory {
    uint32_t address;
    uint32_t size;
    uint32_t counter;
    /* root constants: the push constants' offset, in bytes, or the shader record's. */
    int constants;
    uint32_t push_offset;
    uint8_t record;
} Memory;

static int memory_of(Compiler *c, const Handle *h, Memory *out)
{
    __builtin_memset(out, 0, sizeof(*out));
    const DxbcRootBinding *b = binding_of(c, h);
    if (!h->heap && !b)
        return -1;
    if (b && b->source == DXBC_SOURCE_CONSTANTS) {
        out->constants = 1;
        out->push_offset = b->push_offset;
        out->record = b->record;
        return 0;
    }
    if (b && b->source == DXBC_SOURCE_ADDRESS) {
        out->address = root_address(c, b);
        return 0;
    }
    uint32_t index = heap_index(c, h, b);
    uint32_t record = heap_record(c, index, 0);
    out->address = uvec2_of(c, record, 0);
    out->size = dxil_extract(c, c->t_u32, record, 2);
    if (h->r.cls == CLASS_UAV)
        out->counter = uvec2_of(c, heap_record(c, index, 1), 0);
    return 0;
}

uint32_t dxil_acceleration_structure(Compiler *c, uint32_t handle)
{
    Handle *h = handle_of(c, handle);
    Memory m;
    if (!h || h->r.kind != RK_ACCELERATION_STRUCTURE || memory_of(c, h, &m) || m.constants) {
        dxil_fail(c, "an acceleration structure that is not one");
        return 0;
    }
    return spv_op1(&c->b, SpvOpConvertUToAccelerationStructureKHR,
                   spv_type_opaque(&c->b, SpvOpTypeAccelerationStructureKHR), m.address);
}

static uint32_t word_pointer(Compiler *c, uint32_t address, uint32_t word)
{
    uint32_t ptr = address_pointer(c, address, root_array_pointer(c, c->t_u32, 4));
    uint32_t w[3] = {ptr, dxil_uconst(c, 0), word};
    return spv_op(&c->b, SpvOpAccessChain, spv_type_pointer(&c->b, SpvStorageClassPhysicalStorageBuffer, c->t_u32), w, 3);
}

/* whether a word is inside a view with a record. */
static uint32_t word_in_bounds(Compiler *c, const Memory *m, uint32_t word)
{
    uint32_t words = spv_op2(&c->b, SpvOpShiftRightLogical, c->t_u32, m->size, dxil_uconst(c, 2));
    return spv_op2(&c->b, SpvOpULessThan, c->t_bool, word, words);
}

/* reads a word; out of bounds reads of views with a record give zero. */
static uint32_t read_word(Compiler *c, const Memory *m, uint32_t word)
{
    if (m->constants) {
        uint32_t at = spv_op2(&c->b, SpvOpIAdd, c->t_u32, dxil_uconst(c, m->push_offset),
                              spv_op2(&c->b, SpvOpShiftLeftLogical, c->t_u32, word, dxil_uconst(c, 2)));
        return m->record ? record_word(c, at) : push_word(c, at);
    }
    uint32_t in = 0;
    if (m->size) {
        in = word_in_bounds(c, m, word);
        word = spv_op3(&c->b, SpvOpSelect, c->t_u32, in, word, dxil_uconst(c, 0));
    }
    uint32_t w[3] = {word_pointer(c, m->address, word), SpvMemoryAccessAlignedMask, 4};
    uint32_t v = spv_op(&c->b, SpvOpLoad, c->t_u32, w, 3);
    return in ? spv_op3(&c->b, SpvOpSelect, c->t_u32, in, v, dxil_uconst(c, 0)) : v;
}

/* writes a word; out of bounds writes of views with a record do nothing. */
static void write_word(Compiler *c, const Memory *m, uint32_t word, uint32_t value)
{
    uint32_t merge = 0;
    if (m->size)
        dxil_begin_if(c, word_in_bounds(c, m, word), &merge);
    uint32_t w[4] = {word_pointer(c, m->address, word), value, SpvMemoryAccessAlignedMask, 4};
    spv_op_void(&c->b, SpvOpStore, w, 4);
    if (merge)
        dxil_end_if(c, merge);
}

/* ------------------------------------------------------------------ results */

/* the components of the structure a call returns that some extractvalue reads. */
static uint32_t used_parts(Compiler *c, const DxInst *ins)
{
    uint32_t i = ins->value - c->m.value_count;
    return c->part_masks && i < c->f->value_count ? c->part_masks[i] : ~0u;
}

/* the type of member i of the structure a call returns. */
static uint32_t member_type(Compiler *c, const DxInst *ins, uint32_t i, uint32_t *dx)
{
    const DxType *t = &c->m.types[ins->type];
    uint32_t m = t->kind == DT_STRUCT && i < t->count ? t->members[i] : ins->type;
    if (dx)
        *dx = m;
    return dxil_type(c, m);
}

/* the 64-bit value of two 32-bit words. */
static uint32_t wide_of(Compiler *c, uint32_t lo, uint32_t hi, uint32_t type)
{
    uint32_t pair[2] = {lo, hi};
    return spv_op1(&c->b, SpvOpBitcast, type, dxil_vector(c, c->t_u32, pair, 2));
}

/*
 * a ResRet of a vec4 of a type: its components as the structure's types,
 * and a status of zero. 64-bit texels are two words of the view, as
 * direct3d's R32G32 formats hold them.
 */
static void set_resret(Compiler *c, const DxInst *ins, uint32_t vec4, uint32_t scalar)
{
    uint32_t parts[5];
    uint32_t first = member_type(c, ins, 0, NULL);
    if (first == c->t_u64 || first == c->t_f64) {
        uint32_t u = scalar == c->t_u32 ? vec4 : spv_op1(&c->b, SpvOpBitcast, c->t_uvec[4], vec4);
        parts[0] = wide_of(c, dxil_extract(c, c->t_u32, u, 0), dxil_extract(c, c->t_u32, u, 1), first);
        parts[1] = wide_of(c, dxil_extract(c, c->t_u32, u, 2), dxil_extract(c, c->t_u32, u, 3), first);
        parts[2] = parts[3] = spv_const_null(&c->b, first);
    } else {
        for (uint32_t i = 0; i < 4; i++)
            parts[i] = dxil_convert(c, dxil_extract(c, scalar, vec4, i), scalar, member_type(c, ins, i, NULL));
    }
    parts[4] = dxil_uconst(c, 0);
    dxil_set_parts(c, ins->value, parts, 5);
}

static uint32_t gather_operands(Compiler *c, const DxInst *ins, uint32_t first, uint32_t n, uint32_t scalar);

/* the vec4 of a type a store writes of four values from first; 64-bit values as two words each. */
static uint32_t store_values(Compiler *c, const DxInst *ins, uint32_t first, uint32_t scalar)
{
    const DxValue *v = dx_value(&c->m, c->f, ins->ops[first]);
    uint32_t t = v ? dxil_type(c, v->type) : scalar;
    if (t != c->t_u64 && t != c->t_f64)
        return gather_operands(c, ins, first, 4, scalar);
    uint32_t words[4];
    for (uint32_t i = 0; i < 2; i++) {
        uint32_t id = ins->ops[first + i];
        uint32_t pair = dxil_is_undef(c, id) ? spv_const_null(&c->b, c->t_uvec[2])
                                             : spv_op1(&c->b, SpvOpBitcast, c->t_uvec[2], dxil_value(c, id));
        words[2 * i] = dxil_extract(c, c->t_u32, pair, 0);
        words[2 * i + 1] = dxil_extract(c, c->t_u32, pair, 1);
    }
    uint32_t u = dxil_vector(c, c->t_u32, words, 4);
    return scalar == c->t_u32 ? u : spv_op1(&c->b, SpvOpBitcast, spv_type_vector(&c->b, scalar, 4), u);
}

/* ------------------------------------------------------------------ textures */

static uint32_t coord_count(uint32_t kind)
{
    switch (kind) {
    case RK_TEXTURE1D:
    case RK_TYPED_BUFFER:
    case RK_RAW_BUFFER:
    case RK_STRUCTURED_BUFFER: return 1;
    case RK_TEXTURE1DARRAY:
    case RK_TEXTURE2D:
    case RK_TEXTURE2DMS: return 2;
    case RK_TEXTURECUBEARRAY: return 4;
    default: return 3;
    }
}

static uint32_t spatial_count(uint32_t kind)
{
    switch (kind) {
    case RK_TEXTURE1D:
    case RK_TEXTURE1DARRAY: return 1;
    case RK_TEXTURE3D:
    case RK_TEXTURECUBE:
    case RK_TEXTURECUBEARRAY: return 3;
    default: return 2;
    }
}

/* n operands from first as a vector of the type, undefined ones as zero. */
static uint32_t gather_operands(Compiler *c, const DxInst *ins, uint32_t first, uint32_t n, uint32_t scalar)
{
    uint32_t parts[4];
    for (uint32_t i = 0; i < n; i++) {
        uint32_t id = ins->ops[first + i];
        if (dxil_is_undef(c, id)) {
            parts[i] = spv_const_null(&c->b, scalar);
            continue;
        }
        const DxValue *v = dx_value(&c->m, c->f, id);
        parts[i] = dxil_convert(c, dxil_value(c, id), dxil_type(c, v->type), scalar);
    }
    return dxil_vector(c, scalar, parts, n);
}

/* constant offsets of a sample, or 0 when they are all zero or not constant. */
static uint32_t const_offset(Compiler *c, const DxInst *ins, uint32_t first, uint32_t kind, uint32_t *dynamic)
{
    uint32_t n = spatial_count(kind);
    *dynamic = 0;
    if (kind == RK_TEXTURECUBE || kind == RK_TEXTURECUBEARRAY)
        return 0;
    uint32_t parts[3];
    int any = 0, constant = 1;
    for (uint32_t i = 0; i < n; i++) {
        uint32_t id = ins->ops[first + i];
        if (dxil_is_undef(c, id)) {
            parts[i] = spv_const(&c->b, t_i32(c), 0);
            continue;
        }
        uint64_t k = dxil_constant(c, id, ~0ull);
        if (k == ~0ull) {
            constant = 0;
            continue;
        }
        any |= (uint32_t)k != 0;
        parts[i] = spv_const(&c->b, t_i32(c), (uint32_t)k);
    }
    if (!constant) {
        uint32_t ids[3];
        for (uint32_t i = 0; i < n; i++)
            ids[i] = dxil_is_undef(c, ins->ops[first + i]) ? spv_const(&c->b, t_i32(c), 0)
                                                           : spv_op1(&c->b, SpvOpBitcast, t_i32(c),
                                                                     dxil_value(c, ins->ops[first + i]));
        *dynamic = dxil_vector(c, t_i32(c), ids, n);
        return 0;
    }
    if (!any)
        return 0;
    return n == 1 ? parts[0] : spv_const_composite(&c->b, spv_type_vector(&c->b, t_i32(c), n), parts, n);
}

/* an optional float operand: 0 when undefined or a constant zero. */
static uint32_t optional_float(Compiler *c, uint32_t id)
{
    const DxValue *v = dx_value(&c->m, c->f, id);
    if (!v || v->kind == DV_UNDEF || v->kind == DV_NULL || (v->kind == DV_FLOAT && !v->bits))
        return 0;
    return dxil_convert(c, dxil_value(c, id), dxil_type(c, v->type), c->t_f32);
}

static uint32_t sampled_image(Compiler *c, const Handle *h, const Handle *s, int depth, uint32_t *type)
{
    *type = image_type(c, h, depth, 0);
    uint32_t image = load_image(c, h, *type);
    uint32_t sampler = load_sampler(c, s);
    return nonuniform(c, h->nonuniform ? h : s,
                      spv_op2(&c->b, SpvOpSampledImage, spv_type_sampled_image(&c->b, *type), image, sampler));
}

/*
 * the sample family: plain, bias, level, grad, the comparisons and their
 * level, grad and bias forms. operands after the offsets differ per op.
 */
static int sample(Compiler *c, const DxInst *ins, uint32_t op)
{
    Handle *h = handle_of(c, ins->ops[2]), *s = handle_of(c, ins->ops[3]);
    if (!h || !s)
        return -1;
    int cmp = op == 64 || op == 65 || op == 224 || op == 254 || op == 255;
    uint32_t type;
    uint32_t si = sampled_image(c, h, s, cmp, &type);
    uint32_t n = coord_count(h->r.kind), g = spatial_count(h->r.kind);
    uint32_t w[16], count = 0, operands = 0;
    w[count++] = si;
    w[count++] = gather_operands(c, ins, 4, n, c->t_f32);
    uint32_t at = 11;
    if (cmp)
        w[count++] = dxil_convert(c, dxil_value(c, ins->ops[at++]), c->t_f32, c->t_f32);
    uint32_t lod = 0, bias = 0, ddx = 0, ddy = 0, clamp = 0;
    switch (op) {
    case 60:
    case 64: clamp = optional_float(c, ins->ops[at]); break;
    case 61:
    case 255:
        bias = dxil_value(c, ins->ops[at]);
        clamp = optional_float(c, ins->ops[at + 1]);
        break;
    case 62:
    case 224: lod = dxil_value(c, ins->ops[at]); break;
    case 63:
    case 254:
        ddx = gather_operands(c, ins, at, g, c->t_f32);
        ddy = gather_operands(c, ins, at + 3, g, c->t_f32);
        clamp = optional_float(c, ins->ops[at + 6]);
        break;
    case 65: lod = dxil_fconst(c, 0.0f); break;
    default: break;
    }
    /* implicit derivatives outside a pixel shader: the first level. */
    int implicit = c->kind == KIND_PIXEL && !lod && !ddx;
    if (!implicit && !lod && !ddx) {
        lod = dxil_fconst(c, 0.0f);
        bias = 0;
    }
    uint32_t dynamic, offset = const_offset(c, ins, 8, h->r.kind, &dynamic);
    if (dynamic)
        return dxil_fail(c, "a sample with offsets that are not constant");
    uint32_t mask_at = count++;
    if (bias) {
        operands |= SpvImageOperandsBiasMask;
        w[count++] = bias;
    }
    if (lod) {
        operands |= SpvImageOperandsLodMask;
        w[count++] = lod;
    }
    if (ddx) {
        operands |= SpvImageOperandsGradMask;
        w[count++] = ddx;
        w[count++] = ddy;
    }
    if (offset) {
        operands |= SpvImageOperandsConstOffsetMask;
        w[count++] = offset;
    }
    if (clamp && (implicit || ddx)) {
        spv_capability(&c->b, SpvCapabilityMinLod);
        operands |= SpvImageOperandsMinLodMask;
        w[count++] = clamp;
    }
    w[mask_at] = operands;
    if (!operands)
        count--;
    uint32_t scalar = sampled_type(c, h->r.comp), result;
    if (cmp) {
        uint32_t opcode = implicit ? SpvOpImageSampleDrefImplicitLod : SpvOpImageSampleDrefExplicitLod;
        uint32_t d = spv_op(&c->b, opcode, c->t_f32, w, count);
        uint32_t parts[4] = {d, d, d, d};
        result = dxil_vector(c, c->t_f32, parts, 4);
        scalar = c->t_f32;
    } else {
        uint32_t opcode = implicit ? SpvOpImageSampleImplicitLod : SpvOpImageSampleExplicitLod;
        result = spv_op(&c->b, opcode, spv_type_vector(&c->b, scalar, 4), w, count);
    }
    set_resret(c, ins, nonuniform(c, h, result), scalar);
    return 0;
}

static int gather(Compiler *c, const DxInst *ins, uint32_t op)
{
    Handle *h = handle_of(c, ins->ops[2]), *s = handle_of(c, ins->ops[3]);
    if (!h || !s)
        return -1;
    int cmp = op == 74;
    uint32_t type;
    uint32_t si = sampled_image(c, h, s, cmp, &type);
    uint32_t w[8], count = 0;
    w[count++] = si;
    w[count++] = gather_operands(c, ins, 4, coord_count(h->r.kind), c->t_f32);
    if (cmp)
        w[count++] = dxil_value(c, ins->ops[11]);
    else
        w[count++] = dxil_uconst(c, op == 223 ? 0 : (uint32_t)dxil_constant(c, ins->ops[10], 0));
    uint32_t dynamic, offset = 0;
    uint32_t parts[2];
    int any = 0, constant = 1;
    if (h->r.kind == RK_TEXTURE2D || h->r.kind == RK_TEXTURE2DARRAY) {
        for (uint32_t i = 0; i < 2; i++) {
            uint32_t id = ins->ops[8 + i];
            uint64_t k = dxil_is_undef(c, id) ? 0 : dxil_constant(c, id, ~0ull);
            if (k == ~0ull)
                constant = 0;
            any |= k != 0;
        }
        if (constant && any) {
            for (uint32_t i = 0; i < 2; i++)
                parts[i] = spv_const(&c->b, t_i32(c), (uint32_t)dxil_constant(c, ins->ops[8 + i], 0));
            offset = spv_const_composite(&c->b, spv_type_vector(&c->b, t_i32(c), 2), parts, 2);
        }
    }
    dynamic = 0;
    if (!constant)
        const_offset(c, ins, 8, h->r.kind, &dynamic);
    if (offset) {
        w[count++] = SpvImageOperandsConstOffsetMask;
        w[count++] = offset;
    } else if (dynamic) {
        spv_capability(&c->b, SpvCapabilityImageGatherExtended);
        w[count++] = SpvImageOperandsOffsetMask;
        w[count++] = dynamic;
    }
    uint32_t scalar = cmp ? c->t_f32 : sampled_type(c, h->r.comp);
    uint32_t r = spv_op(&c->b, cmp ? SpvOpImageDrefGather : SpvOpImageGather, spv_type_vector(&c->b, scalar, 4), w, count);
    set_resret(c, ins, nonuniform(c, h, r), scalar);
    return 0;
}

/* textureLoad: texels by integer coordinates, the mip or the sample. */
static int texture_load(Compiler *c, const DxInst *ins)
{
    Handle *h = handle_of(c, ins->ops[2]);
    if (!h)
        return -1;
    uint32_t type = image_type(c, h, 0, 0), scalar = sampled_type(c, h->r.comp);
    uint32_t image = load_image(c, h, type);
    uint32_t w[8], count = 0, operands = 0;
    w[count++] = image;
    w[count++] = gather_operands(c, ins, 4, coord_count(h->r.kind) > 3 ? 3 : coord_count(h->r.kind), c->t_u32);
    uint32_t r;
    int ms = h->r.kind == RK_TEXTURE2DMS || h->r.kind == RK_TEXTURE2DMSARRAY;
    if (h->r.cls == CLASS_UAV) {
        if (ms) {
            w[count++] = SpvImageOperandsSampleMask;
            w[count++] = dxil_value(c, ins->ops[3]);
        }
        r = spv_op(&c->b, SpvOpImageRead, spv_type_vector(&c->b, scalar, 4), w, count);
    } else {
        uint32_t mask_at = count++;
        if (ms) {
            operands |= SpvImageOperandsSampleMask;
            w[count++] = dxil_value(c, ins->ops[3]);
        } else {
            operands |= SpvImageOperandsLodMask;
            w[count++] = dxil_is_undef(c, ins->ops[3]) ? dxil_uconst(c, 0) : dxil_value(c, ins->ops[3]);
        }
        uint32_t dynamic, offset = ms ? 0 : const_offset(c, ins, 7, h->r.kind, &dynamic);
        if (offset) {
            operands |= SpvImageOperandsConstOffsetMask;
            w[count++] = offset;
        }
        w[mask_at] = operands;
        r = spv_op(&c->b, SpvOpImageFetch, spv_type_vector(&c->b, scalar, 4), w, count);
    }
    set_resret(c, ins, nonuniform(c, h, r), scalar);
    return 0;
}

/* textureStore and textureStoreSample: a texel of four values. */
static int texture_store(Compiler *c, const DxInst *ins, int with_sample)
{
    Handle *h = handle_of(c, ins->ops[2]);
    if (!h)
        return -1;
    uint32_t type = image_type(c, h, 0, 0), scalar = sampled_type(c, h->r.comp);
    uint32_t image = load_image(c, h, type);
    uint32_t n = coord_count(h->r.kind);
    uint32_t w[6] = {image, gather_operands(c, ins, 3, n > 3 ? 3 : n, c->t_u32), store_values(c, ins, 6, scalar)};
    uint32_t count = 3;
    if (with_sample) {
        w[count++] = SpvImageOperandsSampleMask;
        w[count++] = dxil_value(c, ins->ops[11]);
    }
    spv_op_void(&c->b, SpvOpImageWrite, w, count);
    return 0;
}

/* ------------------------------------------------------------------ buffers */

static int typed_buffer(const Handle *h) { return h->r.kind == RK_TYPED_BUFFER; }

/* the byte address bufferLoad, rawBufferLoad and the like reach. */
static uint32_t byte_address(Compiler *c, const Handle *h, uint32_t index_op, uint32_t offset_op)
{
    uint32_t a = dxil_value(c, index_op);
    if (h->r.kind == RK_STRUCTURED_BUFFER) {
        uint32_t offset = dxil_is_undef(c, offset_op) ? dxil_uconst(c, 0) : dxil_value(c, offset_op);
        a = spv_op2(&c->b, SpvOpIAdd, c->t_u32, spv_op2(&c->b, SpvOpIMul, c->t_u32, a, dxil_uconst(c, h->r.stride)),
                    offset);
    }
    return a;
}

/* the size in bytes of a dxil scalar type. */
static uint32_t type_bytes(Compiler *c, uint32_t dx)
{
    const DxType *t = &c->m.types[dx];
    if (t->kind == DT_DOUBLE || (t->kind == DT_INT && t->width == 64))
        return 8;
    if ((t->kind == DT_HALF || (t->kind == DT_INT && t->width == 16)) && c->native16)
        return 2;
    return 4;
}

/* a value of a type read from memory at a byte address. */
static uint32_t read_value(Compiler *c, const Memory *m, uint32_t address, uint32_t dx)
{
    uint32_t type = dxil_type(c, dx), bytes = type_bytes(c, dx);
    uint32_t word = spv_op2(&c->b, SpvOpShiftRightLogical, c->t_u32, address, dxil_uconst(c, 2));
    uint32_t lo = read_word(c, m, word);
    if (bytes == 8) {
        uint32_t hi = read_word(c, m, spv_op2(&c->b, SpvOpIAdd, c->t_u32, word, dxil_uconst(c, 1)));
        uint32_t parts[2] = {lo, hi};
        return spv_op1(&c->b, SpvOpBitcast, type, dxil_vector(c, c->t_u32, parts, 2));
    }
    if (bytes == 2) {
        uint32_t shift = spv_op2(&c->b, SpvOpShiftLeftLogical, c->t_u32,
                                 spv_op2(&c->b, SpvOpBitwiseAnd, c->t_u32, address, dxil_uconst(c, 2)), dxil_uconst(c, 3));
        uint32_t v = spv_op2(&c->b, SpvOpShiftRightLogical, c->t_u32, lo, shift);
        uint32_t u16 = spv_op1(&c->b, SpvOpUConvert, c->t_u16, v);
        return type == c->t_u16 ? u16 : spv_op1(&c->b, SpvOpBitcast, type, u16);
    }
    return type == c->t_u32 ? lo : type == c->t_bool ? spv_op2(&c->b, SpvOpINotEqual, c->t_bool, lo, dxil_uconst(c, 0))
                                                     : spv_op1(&c->b, SpvOpBitcast, type, lo);
}

static void write_value(Compiler *c, const Memory *m, uint32_t address, uint32_t value, uint32_t dx)
{
    uint32_t type = dxil_type(c, dx), bytes = type_bytes(c, dx);
    uint32_t word = spv_op2(&c->b, SpvOpShiftRightLogical, c->t_u32, address, dxil_uconst(c, 2));
    if (bytes == 8) {
        uint32_t pair = spv_op1(&c->b, SpvOpBitcast, c->t_uvec[2], value);
        write_word(c, m, word, dxil_extract(c, c->t_u32, pair, 0));
        write_word(c, m, spv_op2(&c->b, SpvOpIAdd, c->t_u32, word, dxil_uconst(c, 1)), dxil_extract(c, c->t_u32, pair, 1));
        return;
    }
    if (bytes == 2) {
        /* half a word: the other half kept, which races as direct3d's 16-bit stores of raw buffers may. */
        uint32_t bits = type == c->t_u16 ? value : spv_op1(&c->b, SpvOpBitcast, c->t_u16, value);
        uint32_t v = spv_op1(&c->b, SpvOpUConvert, c->t_u32, bits);
        uint32_t shift = spv_op2(&c->b, SpvOpShiftLeftLogical, c->t_u32,
                                 spv_op2(&c->b, SpvOpBitwiseAnd, c->t_u32, address, dxil_uconst(c, 2)), dxil_uconst(c, 3));
        uint32_t keep = spv_op1(&c->b, SpvOpNot, c->t_u32,
                                spv_op2(&c->b, SpvOpShiftLeftLogical, c->t_u32, dxil_uconst(c, 0xFFFF), shift));
        uint32_t old = read_word(c, m, word);
        uint32_t merged = spv_op2(&c->b, SpvOpBitwiseOr, c->t_u32, spv_op2(&c->b, SpvOpBitwiseAnd, c->t_u32, old, keep),
                                  spv_op2(&c->b, SpvOpShiftLeftLogical, c->t_u32, v, shift));
        write_word(c, m, word, merged);
        return;
    }
    if (type == c->t_bool)
        value = spv_op3(&c->b, SpvOpSelect, c->t_u32, value, dxil_uconst(c, 1), dxil_uconst(c, 0));
    else if (type != c->t_u32)
        value = spv_op1(&c->b, SpvOpBitcast, c->t_u32, value);
    write_word(c, m, word, value);
}

/* bufferLoad and rawBufferLoad: a typed buffer's texel, or the components a raw or structured view's call uses. */
static int buffer_load(Compiler *c, const DxInst *ins, uint32_t op)
{
    Handle *h = handle_of(c, ins->ops[2]);
    if (!h)
        return -1;
    if (typed_buffer(h)) {
        uint32_t type = image_type(c, h, 0, 0), scalar = sampled_type(c, h->r.comp);
        uint32_t image = load_image(c, h, type);
        uint32_t r = spv_op2(&c->b, h->r.cls == CLASS_UAV ? SpvOpImageRead : SpvOpImageFetch,
                             spv_type_vector(&c->b, scalar, 4), image, dxil_value(c, ins->ops[3]));
        set_resret(c, ins, nonuniform(c, h, r), scalar);
        return 0;
    }
    Memory m;
    if (memory_of(c, h, &m))
        return -1;
    uint32_t base = byte_address(c, h, ins->ops[3], ins->ops[4]);
    uint32_t mask = used_parts(c, ins);
    if (op == 139)
        mask &= (uint32_t)dxil_constant(c, ins->ops[5], 0xF) | 0x10;
    uint32_t parts[5];
    for (uint32_t i = 0; i < 4; i++) {
        uint32_t dx, type = member_type(c, ins, i, &dx);
        if (!(mask & (1u << i))) {
            parts[i] = spv_const_null(&c->b, type);
            continue;
        }
        uint32_t address = spv_op2(&c->b, SpvOpIAdd, c->t_u32, base, dxil_uconst(c, i * type_bytes(c, dx)));
        parts[i] = read_value(c, &m, address, dx);
    }
    parts[4] = dxil_uconst(c, 0);
    dxil_set_parts(c, ins->value, parts, 5);
    return 0;
}

/* bufferStore and rawBufferStore: the masked components of a value. */
static int buffer_store(Compiler *c, const DxInst *ins)
{
    Handle *h = handle_of(c, ins->ops[2]);
    if (!h)
        return -1;
    uint32_t mask = (uint32_t)dxil_constant(c, ins->ops[9], 0xF);
    if (typed_buffer(h)) {
        uint32_t type = image_type(c, h, 0, 0), scalar = sampled_type(c, h->r.comp);
        uint32_t image = load_image(c, h, type);
        uint32_t w[3] = {image, dxil_value(c, ins->ops[3]), store_values(c, ins, 5, scalar)};
        spv_op_void(&c->b, SpvOpImageWrite, w, 3);
        return 0;
    }
    Memory m;
    if (memory_of(c, h, &m))
        return -1;
    uint32_t base = byte_address(c, h, ins->ops[3], ins->ops[4]);
    for (uint32_t i = 0; i < 4; i++) {
        if (!(mask & (1u << i)))
            continue;
        const DxValue *v = dx_value(&c->m, c->f, ins->ops[5 + i]);
        if (!v)
            return dxil_fail(c, "a store of nothing");
        uint32_t address = spv_op2(&c->b, SpvOpIAdd, c->t_u32, base, dxil_uconst(c, i * type_bytes(c, v->type)));
        write_value(c, &m, address, dxil_value(c, ins->ops[5 + i]), v->type);
    }
    return 0;
}

/* cbufferLoadLegacy: a row of 16 bytes, as the structure's members split it. */
static int cbuffer_load(Compiler *c, const DxInst *ins, uint32_t op)
{
    Handle *h = handle_of(c, ins->ops[2]);
    if (!h)
        return -1;
    Memory m;
    if (memory_of(c, h, &m))
        return -1;
    if (op == 58) {
        /* cbufferLoad: one value at a byte offset. */
        uint32_t v = read_value(c, &m, dxil_value(c, ins->ops[3]), ins->type);
        dxil_set_plain(c, ins->value, v);
        return 0;
    }
    uint32_t row = dxil_value(c, ins->ops[3]);
    const DxType *t = &c->m.types[ins->type];
    uint32_t count = t->kind == DT_STRUCT ? t->count : 1;
    if (count > 8)
        return dxil_fail(c, "a constant buffer row of too many members");
    uint32_t bytes = type_bytes(c, t->kind == DT_STRUCT ? t->members[0] : ins->type);
    uint32_t mask = used_parts(c, ins), parts[8], words[4] = {0, 0, 0, 0}, needed = 0;
    for (uint32_t i = 0; i < count; i++)
        if (mask & (1u << i))
            needed |= bytes == 8 ? 3u << (2 * i) : 1u << (i * bytes / 4);
    /* the row's words: from the push constants, or one load of 16 bytes, rows past a view's size reading zero. */
    if (m.constants) {
        uint32_t base = spv_op2(&c->b, SpvOpIAdd, c->t_u32, dxil_uconst(c, m.push_offset),
                                spv_op2(&c->b, SpvOpShiftLeftLogical, c->t_u32, row, dxil_uconst(c, 4)));
        for (uint32_t i = 0; i < 4; i++)
            if (needed & (1u << i))
                words[i] = m.record ? record_word(c, spv_op2(&c->b, SpvOpIAdd, c->t_u32, base, dxil_uconst(c, 4 * i)))
                                    : push_word(c, spv_op2(&c->b, SpvOpIAdd, c->t_u32, base, dxil_uconst(c, 4 * i)));
    } else if (needed) {
        uint32_t v4 = c->t_uvec[4], in = 0;
        if (m.size) {
            in = spv_op2(&c->b, SpvOpULessThan, c->t_bool, row,
                         spv_op2(&c->b, SpvOpShiftRightLogical, c->t_u32, m.size, dxil_uconst(c, 4)));
            row = spv_op3(&c->b, SpvOpSelect, c->t_u32, in, row, dxil_uconst(c, 0));
        }
        uint32_t ptr = address_pointer(c, m.address, root_array_pointer(c, v4, 16));
        uint32_t w[3] = {ptr, dxil_uconst(c, 0), row};
        uint32_t elem =
            spv_op(&c->b, SpvOpAccessChain, spv_type_pointer(&c->b, SpvStorageClassPhysicalStorageBuffer, v4), w, 3);
        uint32_t l[3] = {elem, SpvMemoryAccessAlignedMask, 16};
        uint32_t v = spv_op(&c->b, SpvOpLoad, v4, l, 3);
        if (in)
            v = spv_op3(&c->b, SpvOpSelect, v4, in, v, spv_const_null(&c->b, v4));
        for (uint32_t i = 0; i < 4; i++)
            if (needed & (1u << i))
                words[i] = dxil_extract(c, c->t_u32, v, i);
    }
    for (uint32_t i = 0; i < count; i++) {
        uint32_t type = member_type(c, ins, i, NULL);
        if (!(mask & (1u << i))) {
            parts[i] = spv_const_null(&c->b, type);
        } else if (bytes == 8) {
            uint32_t pair[2] = {words[2 * i], words[2 * i + 1]};
            parts[i] = spv_op1(&c->b, SpvOpBitcast, type, dxil_vector(c, c->t_u32, pair, 2));
        } else if (bytes == 2) {
            uint32_t half = spv_op2(&c->b, SpvOpShiftRightLogical, c->t_u32, words[i / 2], dxil_uconst(c, 16 * (i & 1)));
            uint32_t u16 = spv_op1(&c->b, SpvOpUConvert, c->t_u16, half);
            parts[i] = type == c->t_u16 ? u16 : spv_op1(&c->b, SpvOpBitcast, type, u16);
        } else {
            parts[i] = type == c->t_u32 ? words[i] : type == c->t_bool
                       ? spv_op2(&c->b, SpvOpINotEqual, c->t_bool, words[i], dxil_uconst(c, 0))
                       : spv_op1(&c->b, SpvOpBitcast, type, words[i]);
        }
    }
    dxil_set_parts(c, ins->value, parts, count);
    return 0;
}

/* getDimensions: sizes, then levels or samples, as the kind answers them. */
static int dimensions(Compiler *c, const DxInst *ins)
{
    Handle *h = handle_of(c, ins->ops[2]);
    if (!h)
        return -1;
    uint32_t parts[4], zero = dxil_uconst(c, 0);
    parts[0] = parts[1] = parts[2] = parts[3] = zero;
    if (h->r.kind == RK_RAW_BUFFER || h->r.kind == RK_STRUCTURED_BUFFER) {
        Memory m;
        if (memory_of(c, h, &m))
            return -1;
        uint32_t size = m.size ? m.size : zero;
        if (h->r.kind == RK_STRUCTURED_BUFFER && h->r.stride)
            size = spv_op2(&c->b, SpvOpUDiv, c->t_u32, size, dxil_uconst(c, h->r.stride));
        parts[0] = size;
        dxil_set_parts(c, ins->value, parts, 4);
        return 0;
    }
    spv_capability(&c->b, SpvCapabilityImageQuery);
    uint32_t type = image_type(c, h, 0, 0);
    uint32_t image = load_image(c, h, type);
    uint32_t dims;
    switch (h->r.kind) {
    case RK_TEXTURE1D:
    case RK_TYPED_BUFFER: dims = 1; break;
    case RK_TEXTURE3D:
    case RK_TEXTURE2DARRAY:
    case RK_TEXTURE2DMSARRAY:
    case RK_TEXTURECUBEARRAY: dims = 3; break;
    default: dims = 2; break;
    }
    int ms = h->r.kind == RK_TEXTURE2DMS || h->r.kind == RK_TEXTURE2DMSARRAY;
    uint32_t vtype = dims == 1 ? c->t_u32 : spv_type_vector(&c->b, c->t_u32, dims), size;
    if (h->r.cls == CLASS_UAV || ms || h->r.kind == RK_TYPED_BUFFER) {
        size = spv_op1(&c->b, SpvOpImageQuerySize, vtype, image);
        if (ms)
            parts[3] = spv_op1(&c->b, SpvOpImageQuerySamples, c->t_u32, image);
    } else {
        uint32_t lod = dxil_is_undef(c, ins->ops[3]) ? zero : dxil_value(c, ins->ops[3]);
        size = spv_op2(&c->b, SpvOpImageQuerySizeLod, vtype, image, lod);
        parts[3] = spv_op1(&c->b, SpvOpImageQueryLevels, c->t_u32, image);
    }
    for (uint32_t i = 0; i < dims; i++)
        parts[i] = dims == 1 ? size : dxil_extract(c, c->t_u32, size, i);
    /* a cube array's layers count six faces each. */
    if (h->r.kind == RK_TEXTURECUBEARRAY)
        parts[2] = spv_op2(&c->b, SpvOpUDiv, c->t_u32, parts[2], dxil_uconst(c, 6));
    dxil_set_parts(c, ins->value, parts, 4);
    return 0;
}

static int calculate_lod(Compiler *c, const DxInst *ins)
{
    Handle *h = handle_of(c, ins->ops[2]), *s = handle_of(c, ins->ops[3]);
    if (!h || !s)
        return -1;
    spv_capability(&c->b, SpvCapabilityImageQuery);
    uint32_t type;
    uint32_t si = sampled_image(c, h, s, 0, &type);
    uint32_t coord = gather_operands(c, ins, 4, spatial_count(h->r.kind), c->t_f32);
    uint32_t v2 = spv_op2(&c->b, SpvOpImageQueryLod, c->t_vec[2], si, coord);
    uint32_t clamped = (uint32_t)dxil_constant(c, ins->ops[7], 1) & 1;
    dxil_set_plain(c, ins->value, dxil_extract(c, c->t_f32, v2, clamped ? 0 : 1));
    return 0;
}

/* ------------------------------------------------------------------ atomics */

static uint32_t atomic_opcode(uint32_t op)
{
    static const uint32_t OPS[9] = {SpvOpAtomicIAdd, SpvOpAtomicAnd,  SpvOpAtomicOr,   SpvOpAtomicXor,     SpvOpAtomicSMin,
                                    SpvOpAtomicSMax, SpvOpAtomicUMin, SpvOpAtomicUMax, SpvOpAtomicExchange};
    return op < 9 ? OPS[op] : SpvOpAtomicIAdd;
}

/* a pointer to the value an atomic works on, and whether it is in bounds, 0 when it always is. */
static uint32_t atomic_pointer(Compiler *c, const Handle *h, const DxInst *ins, uint32_t first, uint32_t *type,
                               uint32_t *in)
{
    *in = 0;
    if (h->r.kind == RK_RAW_BUFFER || h->r.kind == RK_STRUCTURED_BUFFER) {
        Memory m;
        if (memory_of(c, h, &m))
            return 0;
        uint32_t address = byte_address(c, h, ins->ops[first], ins->ops[first + 1]);
        if (*type == c->t_u64) {
            /* a qword, its index checked against the size as the words' are. */
            spv_capability(&c->b, SpvCapabilityInt64Atomics);
            uint32_t qword = spv_op2(&c->b, SpvOpShiftRightLogical, c->t_u32, address, dxil_uconst(c, 3));
            if (m.size) {
                uint32_t qwords = spv_op2(&c->b, SpvOpShiftRightLogical, c->t_u32, m.size, dxil_uconst(c, 3));
                *in = spv_op2(&c->b, SpvOpULessThan, c->t_bool, qword, qwords);
                qword = spv_op3(&c->b, SpvOpSelect, c->t_u32, *in, qword, dxil_uconst(c, 0));
            }
            uint32_t ptr = address_pointer(c, m.address, root_array_pointer(c, c->t_u64, 8));
            uint32_t w[3] = {ptr, dxil_uconst(c, 0), qword};
            return spv_op(&c->b, SpvOpAccessChain, spv_type_pointer(&c->b, SpvStorageClassPhysicalStorageBuffer, c->t_u64),
                          w, 3);
        }
        uint32_t word = spv_op2(&c->b, SpvOpShiftRightLogical, c->t_u32, address, dxil_uconst(c, 2));
        if (m.size) {
            *in = word_in_bounds(c, &m, word);
            word = spv_op3(&c->b, SpvOpSelect, c->t_u32, *in, word, dxil_uconst(c, 0));
        }
        return word_pointer(c, m.address, word);
    }
    uint32_t image, ptr, scalar;
    if (*type == c->t_u64) {
        /* 64-bit texels: the heap's views of them, beside its own. */
        int is_signed = h->r.comp == COMP_I64;
        scalar = spv_type_int(&c->b, 64, (uint32_t)is_signed);
        image = wide_image_type(c, h, scalar, is_signed);
        ptr = wide_image_pointer(c, h, image);
    } else {
        image = image_type(c, h, 0, 1);
        ptr = image_pointer(c, h, image);
        scalar = sampled_type(c, h->r.comp);
    }
    if (!ptr)
        return 0;
    *type = scalar;
    uint32_t n = coord_count(h->r.kind);
    uint32_t w[3] = {ptr, gather_operands(c, ins, first, n > 3 ? 3 : n, c->t_u32), dxil_uconst(c, 0)};
    return spv_op(&c->b, SpvOpImageTexelPointer, spv_type_pointer(&c->b, SpvStorageClassImage, scalar), w, 3);
}

static int atomic(Compiler *c, const DxInst *ins, int exchange)
{
    Handle *h = handle_of(c, ins->ops[2]);
    if (!h)
        return -1;
    uint32_t result_type = dxil_type(c, ins->type), type = result_type, in;
    uint32_t first = exchange ? 3 : 4;
    uint32_t ptr = atomic_pointer(c, h, ins, first, &type, &in);
    if (!ptr)
        return -1;
    /* out of bounds atomics of views with a record do nothing and return zero. */
    uint32_t before = c->b.block, merge = 0;
    if (in)
        dxil_begin_if(c, in, &merge);
    uint32_t scope = dxil_uconst(c, SpvScopeDevice), none = dxil_uconst(c, 0), r;
    if (type == c->t_f32)
        return dxil_fail(c, "a float atomic");
    if (exchange) {
        uint32_t cmp = dxil_convert(c, dxil_value(c, ins->ops[6]), result_type, type);
        uint32_t value = dxil_convert(c, dxil_value(c, ins->ops[7]), result_type, type);
        uint32_t w[6] = {ptr, scope, none, none, value, cmp};
        r = spv_op(&c->b, SpvOpAtomicCompareExchange, type, w, 6);
    } else {
        uint32_t op = (uint32_t)dxil_constant(c, ins->ops[3], 0);
        uint32_t value = dxil_convert(c, dxil_value(c, ins->ops[7]), result_type, type);
        uint32_t w[4] = {ptr, scope, none, value};
        r = spv_op(&c->b, atomic_opcode(op), type, w, 4);
    }
    if (merge) {
        uint32_t then = c->b.block;
        dxil_end_if(c, merge);
        uint32_t w[4] = {r, then, spv_const_null(&c->b, type), before};
        r = spv_op(&c->b, SpvOpPhi, type, w, 4);
    }
    dxil_set_plain(c, ins->value, dxil_convert(c, r, type, result_type));
    return 0;
}

/* bufferUpdateCounter: the view's hidden counter, the old value when counting up, the new one down. */
static int update_counter(Compiler *c, const DxInst *ins)
{
    Handle *h = handle_of(c, ins->ops[2]);
    if (!h)
        return -1;
    Memory m;
    if (memory_of(c, h, &m))
        return -1;
    if (!m.counter)
        return dxil_fail(c, "a counter of a root descriptor");
    uint32_t ptr = word_pointer(c, m.counter, dxil_uconst(c, 0));
    uint32_t a[3] = {ptr, dxil_uconst(c, SpvScopeDevice), dxil_uconst(c, 0)};
    uint32_t r;
    if ((int8_t)dxil_constant(c, ins->ops[3], 1) > 0) {
        r = spv_op(&c->b, SpvOpAtomicIIncrement, c->t_u32, a, 3);
    } else {
        r = spv_op(&c->b, SpvOpAtomicIDecrement, c->t_u32, a, 3);
        r = spv_op2(&c->b, SpvOpISub, c->t_u32, r, dxil_uconst(c, 1));
    }
    dxil_set_plain(c, ins->value, r);
    return 0;
}

/* barrier's mode: 1 syncs the group, 2 fences device memory, 4 the group's views, 8 shared memory. */
static int barrier(Compiler *c, uint32_t mode)
{
    uint32_t scope = (mode & 2) ? SpvScopeDevice : SpvScopeWorkgroup;
    uint32_t semantics = SpvMemorySemanticsAcquireReleaseMask;
    if (mode & 8)
        semantics |= SpvMemorySemanticsWorkgroupMemoryMask;
    if (mode & 6)
        semantics |= SpvMemorySemanticsImageMemoryMask | SpvMemorySemanticsUniformMemoryMask;
    if (!(mode & 14))
        semantics = 0;
    if (mode & 1) {
        uint32_t w[3] = {dxil_uconst(c, SpvScopeWorkgroup), dxil_uconst(c, scope), dxil_uconst(c, semantics)};
        spv_op_void(&c->b, SpvOpControlBarrier, w, 3);
    } else if (semantics) {
        uint32_t w[2] = {dxil_uconst(c, scope), dxil_uconst(c, semantics)};
        spv_op_void(&c->b, SpvOpMemoryBarrier, w, 2);
    }
    return 0;
}

/* ------------------------------------------------------------------ sample positions */

/*
 * direct3d's standard sample positions, in sixteenths of a pixel from its
 * center, for 1, 2, 4, 8 and 16 samples: those of count n start at n - 1.
 */
static const int8_t SAMPLE_POSITIONS[31][2] = {
    {0, 0},   {4, 4},   {-4, -4}, {-2, -6}, {6, -2}, {-6, 2}, {2, 6},  {1, -3},  {-1, 3},  {5, 1},   {-3, -5},
    {-5, 5},  {-7, -1}, {3, 7},   {7, -7},  {1, 1},  {-1, -3}, {-3, 2}, {4, -1},  {-5, -2}, {2, 5},   {5, 3},
    {3, -5},  {-2, 6},  {0, -7},  {-4, -6}, {-6, 4}, {-8, 0},  {7, -4}, {6, 7},   {-7, -8}};

static int sample_position(Compiler *c, const DxInst *ins, uint32_t count, uint32_t sample)
{
    uint32_t v2 = c->t_vec[2];
    if (!c->sample_positions) {
        uint32_t parts[31];
        for (uint32_t i = 0; i < 31; i++) {
            uint32_t xy[2] = {dxil_fconst(c, SAMPLE_POSITIONS[i][0] / 16.0f), dxil_fconst(c, SAMPLE_POSITIONS[i][1] / 16.0f)};
            parts[i] = spv_const_composite(&c->b, v2, xy, 2);
        }
        uint32_t array = spv_type_array(&c->b, v2, 31);
        c->sample_positions =
            dxil_global(c, SpvStorageClassPrivate, array, spv_const_composite_array(&c->b, array, parts, 31));
    }
    uint32_t table = c->sample_positions;
    uint32_t one = dxil_uconst(c, 1);
    uint32_t pow2 = spv_op2(&c->b, SpvOpIEqual, c->t_bool,
                            spv_op2(&c->b, SpvOpBitwiseAnd, c->t_u32, count, spv_op2(&c->b, SpvOpISub, c->t_u32, count, one)),
                            dxil_uconst(c, 0));
    uint32_t valid = spv_op2(&c->b, SpvOpLogicalAnd, c->t_bool, pow2,
                             spv_op2(&c->b, SpvOpLogicalAnd, c->t_bool,
                                     spv_op2(&c->b, SpvOpULessThan, c->t_bool, sample, count),
                                     spv_op2(&c->b, SpvOpULessThanEqual, c->t_bool, count, dxil_uconst(c, 16))));
    uint32_t index = spv_op3(&c->b, SpvOpSelect, c->t_u32, valid,
                             spv_op2(&c->b, SpvOpIAdd, c->t_u32, spv_op2(&c->b, SpvOpISub, c->t_u32, count, one), sample),
                             dxil_uconst(c, 0));
    uint32_t w[2] = {table, index};
    uint32_t xy = spv_load(&c->b, v2, spv_op(&c->b, SpvOpAccessChain, spv_type_pointer(&c->b, SpvStorageClassPrivate, v2), w, 2));
    uint32_t out[2] = {dxil_extract(c, c->t_f32, xy, 0), dxil_extract(c, c->t_f32, xy, 1)};
    for (uint32_t i = 0; i < 2; i++)
        out[i] = dxil_convert(c, out[i], c->t_f32, member_type(c, ins, i, NULL));
    dxil_set_parts(c, ins->value, out, 2);
    return 0;
}

int dxil_resource_op(Compiler *c, const DxInst *ins, uint32_t op)
{
    switch (op) {
    case 57: return create_handle(c, ins) ? -1 : 1;
    case 160: return create_for_lib(c, ins) ? -1 : 1;
    case 216: return annotate(c, ins) ? -1 : 1;
    case 217: return create_from_binding(c, ins) ? -1 : 1;
    case 218: return create_from_heap(c, ins) ? -1 : 1;
    case 58:
    case 59: return cbuffer_load(c, ins, op) ? -1 : 1;
    case 60:
    case 61:
    case 62:
    case 63:
    case 64:
    case 65:
    case 224:
    case 254:
    case 255: return sample(c, ins, op) ? -1 : 1;
    case 66: return texture_load(c, ins) ? -1 : 1;
    case 67: return texture_store(c, ins, 0) ? -1 : 1;
    case 225: return texture_store(c, ins, 1) ? -1 : 1;
    case 68:
    case 139: return buffer_load(c, ins, op) ? -1 : 1;
    case 69:
    case 140: return buffer_store(c, ins) ? -1 : 1;
    case 70: return update_counter(c, ins) ? -1 : 1;
    case 71: dxil_set_plain(c, ins->value, spv_const_bool(&c->b, 1)); return 1;
    case 72: return dimensions(c, ins) ? -1 : 1;
    case 73:
    case 74:
    case 223: return gather(c, ins, op) ? -1 : 1;
    case 75: {
        Handle *h = handle_of(c, ins->ops[2]);
        if (!h)
            return -1;
        spv_capability(&c->b, SpvCapabilityImageQuery);
        uint32_t type = image_type(c, h, 0, 0);
        uint32_t count = spv_op1(&c->b, SpvOpImageQuerySamples, c->t_u32, load_image(c, h, type));
        return sample_position(c, ins, count, dxil_value(c, ins->ops[3])) ? -1 : 1;
    }
    case 76: {
        uint32_t n = c->options->sample_count ? c->options->sample_count : 1;
        return sample_position(c, ins, dxil_uconst(c, n), dxil_value(c, ins->ops[2])) ? -1 : 1;
    }
    case 78: return atomic(c, ins, 0) ? -1 : 1;
    case 79: return atomic(c, ins, 1) ? -1 : 1;
    case 80: return barrier(c, (uint32_t)dxil_constant(c, ins->ops[2], 0)) ? -1 : 1;
    case 81: return calculate_lod(c, ins) ? -1 : 1;
    default: return 0;
    }
}
