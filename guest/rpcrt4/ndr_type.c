/*
 * the aggregate types of the ndr engine: structures, arrays, strings and unions.
 *
 * dce wire layouts:
 *   conformant arrays, strings and structures ending in one put their element count first. nested ones put all their
 *   counts at the head of the outermost one.
 *   varying ones put an offset and a length before their elements.
 *   structures and arrays of one simple kind are copied whole, others are walked element by element.
 *   pointers write a referent in place and their pointees follow the outermost construct, in order.
 */
#include "ndr.h"

/* ------------------------------------------------------------------ memory sizes */

static ULONG_PTR conf_memory(NDRC *c, PFORMAT_STRING f, ULONG dim);

/*
 * the memory a type takes, as a walker steps over struct members or array elements. a conformant array has no size of
 * its own, it has as many elements as the walk's counts say.
 */
ULONG NdrpMemorySize(NDRC *c, uchar *memory, PFORMAT_STRING f)
{
    uchar fc = f[0];
    if (NdrSimpleMem[fc])
        return NdrSimpleMem[fc];
    switch (fc) {
    case FC_RP:
    case FC_UP:
    case FC_OP:
    case FC_FP:
    case FC_IP:
    case FC_BYTE_COUNT_POINTER:
        return sizeof(void *);
    case FC_STRUCT:
    case FC_PSTRUCT:
    case FC_CSTRUCT:
    case FC_CPSTRUCT:
    case FC_CVSTRUCT:
    case FC_BOGUS_STRUCT:
    case FC_HARD_STRUCT:
    case FC_SMFARRAY:
    case FC_SMVARRAY:
    case FC_CSTRING:
    case FC_BSTRING:
        return *(const USHORT *)(f + 2);
    case FC_WSTRING:
        return 2 * (ULONG)*(const USHORT *)(f + 2);
    case FC_SSTRING:
        return f[1] * (ULONG)*(const USHORT *)(f + 3);
    case FC_ENCAPSULATED_UNION:
        /* the switch comes first, so the format's size is that of the arms. */
        return *(const USHORT *)(f + 2) + (f[1] >> 4);
    case FC_LGFARRAY:
    case FC_LGVARRAY:
        return *(const ULONG *)(f + 2);
    case FC_NON_ENCAPSULATED_UNION: {
        PFORMAT_STRING d = f + 2 + NDR_DESC_SIZE(c->M);
        d += *(const short *)d;
        return *(const USHORT *)d;
    }
    case FC_TRANSMIT_AS:
    case FC_REPRESENT_AS:
    case FC_USER_MARSHAL:
        return *(const USHORT *)(f + 4);
    case FC_RANGE:
        return NdrSimpleMem[f[1] & 0x0F];
    case FC_BOGUS_ARRAY: {
        ULONG n = *(const USHORT *)(f + 2);
        return (ULONG)(n * (ULONG_PTR)NdrpMemorySize(c, memory, f + 4 + 2 * NDR_DESC_SIZE(c->M)));
    }
    case FC_CARRAY:
    case FC_CVARRAY:
        return (ULONG)(c->M->MaxCount * *(const USHORT *)(f + 2));
    case FC_PP:
    case FC_EMBEDDED_COMPLEX:
        if (fc == FC_EMBEDDED_COMPLEX)
            return NdrpMemorySize(c, memory, f + 2 + *(const short *)(f + 2));
        return 0;
    default:
        NDR_RAISE(RPC_S_INTERNAL_ERROR);
        return 0;
    }
}

/*
 * the memory the stub gives an [out] object before the call. conformant counts come from the parameters already
 * unmarshaled.
 */
SIZE_T NdrpOutMemory(PMIDL_STUB_MESSAGE m, PFORMAT_STRING f)
{
    NDRC c = {m, NDR_SIZE, nullptr};
    ULONG incr = NDR_DESC_SIZE(m);
    switch (f[0]) {
    case FC_CARRAY:
    case FC_CVARRAY:
        return NdrpCheckedProduct(NdrpComputeConformance(m, nullptr, f + 4), *(const USHORT *)(f + 2));
    case FC_C_CSTRING:
    case FC_C_BSTRING:
    case FC_C_WSTRING:
        if (f[1] != FC_STRING_SIZED)
            NDR_RAISE(RPC_X_BAD_STUB_DATA);
        return NdrpCheckedProduct(NdrpComputeConformance(m, nullptr, f + 2), f[0] == FC_C_WSTRING ? 2 : 1);
    case FC_BOGUS_ARRAY: {
        PFORMAT_STRING e = f + 4 + 2 * incr;
        ULONG elem;
        if (e[0] == FC_EMBEDDED_COMPLEX)
            elem = NdrpMemorySize(&c, nullptr, e + 2 + *(const short *)(e + 2));
        else if (e[0] >= FC_RP && e[0] <= FC_FP)
            elem = sizeof(void *);
        else
            elem = NdrpMemorySize(&c, nullptr, e);
        if (*(const ULONG *)(f + 4) == 0xFFFFFFFFu)
            return NdrpCheckedProduct(*(const USHORT *)(f + 2), elem);
        return NdrpCheckedProduct(NdrpComputeConformance(m, nullptr, f + 4), elem);
    }
    default:
        return NdrpMemorySize(&c, nullptr, f);
    }
}

/* ------------------------------------------------------------------ conformance counts */

/*
 * how many conformance counts a type puts at the head of an outermost construct: its own and those of arrays inside its
 * elements. a fixed array nested in a conformant one counts as a dimension too.
 */
static ULONG dims_of(PMIDL_STUB_MESSAGE m, PFORMAT_STRING f, BOOL for_variance)
{
    ULONG incr = NDR_DESC_SIZE(m);
    switch (f[0]) {
    case FC_CARRAY:
    case FC_CVARRAY:
    case FC_SMFARRAY:
    case FC_LGFARRAY:
    case FC_SMVARRAY:
    case FC_LGVARRAY:
        return 1;
    case FC_C_CSTRING:
    case FC_C_BSTRING:
    case FC_C_SSTRING:
    case FC_C_WSTRING:
        return for_variance ? 0 : 1;
    case FC_CSTRUCT:
    case FC_CPSTRUCT:
    case FC_CVSTRUCT:
        return dims_of(m, f + 4 + *(const short *)(f + 4), for_variance);
    case FC_BOGUS_STRUCT:
        return *(const short *)(f + 4) ? dims_of(m, f + 4 + *(const short *)(f + 4), for_variance) : 0;
    case FC_BOGUS_ARRAY: {
        ULONG n = 1;
        PFORMAT_STRING e = f + 4 + 2 * incr;
        while (e[0] == FC_EMBEDDED_COMPLEX) {
            PFORMAT_STRING t = e + 2 + *(const short *)(e + 2);
            if (t[0] == FC_SMFARRAY) {
                n++;
                e = t + 4;
            } else if (t[0] == FC_LGFARRAY) {
                n++;
                e = t + 6;
            } else if (t[0] == FC_BOGUS_ARRAY) {
                n++;
                e = t + 4 + 2 * incr;
            } else if (t[0] >= FC_C_CSTRING && t[0] <= FC_WSTRING) {
                if (!for_variance)
                    n++;
                break;
            } else {
                break;
            }
        }
        return n;
    }
    default:
        return 0;
    }
}

/* the conformant part of a structure: its memory offset and its format. */
static PFORMAT_STRING conformant_tail(PFORMAT_STRING f, ULONG *offset)
{
    *offset = 0;
    switch (f[0]) {
    case FC_CSTRUCT:
    case FC_CPSTRUCT:
    case FC_CVSTRUCT:
        *offset = *(const USHORT *)(f + 2);
        return f + 4 + *(const short *)(f + 4);
    case FC_BOGUS_STRUCT:
        return *(const short *)(f + 4) ? f + 4 + *(const short *)(f + 4) : nullptr;
    default:
        return nullptr;
    }
}

/* the memory a conformant type's elements take, per the walk's counts. dim is the index of its count. */
static ULONG_PTR conf_memory(NDRC *c, PFORMAT_STRING f, ULONG dim)
{
    NDR_QUEUE *q = c->Q;
    ULONG incr = NDR_DESC_SIZE(c->M);
    if (dim >= NDR_MAX_DIMS)
        NDR_RAISE(RPC_S_INTERNAL_ERROR);
    switch (f[0]) {
    case FC_CARRAY:
    case FC_CVARRAY:
        return q->Conf[dim] * *(const USHORT *)(f + 2);
    case FC_C_CSTRING:
    case FC_C_BSTRING:
        return q->Conf[dim];
    case FC_C_WSTRING:
        return q->Conf[dim] * 2;
    case FC_C_SSTRING:
        return q->Conf[dim] * f[1];
    case FC_BOGUS_ARRAY: {
        PFORMAT_STRING e = f + 4 + 2 * incr;
        ULONG_PTR each;
        if (e[0] == FC_EMBEDDED_COMPLEX) {
            PFORMAT_STRING t = e + 2 + *(const short *)(e + 2);
            if (t[0] == FC_BOGUS_ARRAY || t[0] == FC_SMFARRAY || t[0] == FC_LGFARRAY) {
                each = t[0] == FC_SMFARRAY ? *(const USHORT *)(t + 2)
                       : t[0] == FC_LGFARRAY ? *(const ULONG *)(t + 2)
                                             : conf_memory(c, t, dim + 1);
            } else {
                each = NdrpMemorySize(c, nullptr, t);
            }
        } else {
            each = NdrpMemorySize(c, nullptr, e);
        }
        return q->Conf[dim] * each;
    }
    case FC_CSTRUCT:
    case FC_CPSTRUCT:
    case FC_CVSTRUCT:
    case FC_BOGUS_STRUCT: {
        ULONG offset;
        PFORMAT_STRING tail = conformant_tail(f, &offset);
        return *(const USHORT *)(f + 2) + (tail ? conf_memory(c, tail, dim) : 0);
    }
    default:
        return 0;
    }
}

/*
 * the element count of a conformant array or string. if an enclosing construct reserved the counts, it goes in its reserved
 * slot, else at the cursor. descriptor is the correlation descriptor, or null when only the data gives the count, as for
 * an unsized string.
 */
static ULONG_PTR conf_count(NDRC *c, uchar *memory, PFORMAT_STRING descriptor, ULONG flags, ULONG_PTR known)
{
    NDR_QUEUE *q = c->Q;
    ULONG_PTR count = known;
    BOOL computed = descriptor && !NDR_WIRE_READ(c->Pass);
    if (computed)
        count = NdrpComputeConformance(c->M, memory, descriptor);
    if (flags & NDRF_CONF_DONE) {
        ULONG index = q->Next++;
        if (index >= q->Dims)
            NDR_RAISE(RPC_S_INTERNAL_ERROR);
        if (NDR_WIRE_READ(c->Pass))
            return q->Conf[index];
        if (c->Pass == NDR_MARSHAL && q->Slots)
            q->Slots[index] = (ULONG)count;
        return count;
    }
    if (NDR_WIRE_READ(c->Pass))
        return NdrpU32(c, 0);
    if (c->Pass == NDR_FREE)
        return count;
    NdrpU32(c, (ULONG)count);
    return count;
}

/* an outermost construct reserves the counts of its dimensions at the cursor. */
typedef struct {
    ULONG Next;
    ULONG Dims;
    ULONG *Slots;
    BOOL Reserved;
} CONF_SAVE;

static void conf_reserve(NDRC *c, ULONG dims, CONF_SAVE *save)
{
    NDR_QUEUE *q = c->Q;
    save->Next = q->Next;
    save->Dims = q->Dims;
    save->Slots = q->Slots;
    save->Reserved = TRUE;
    if (dims > NDR_MAX_DIMS)
        NDR_RAISE(RPC_S_INVALID_BOUND);
    q->Dims = dims;
    q->Next = 0;
    q->Slots = nullptr;
    if (c->Pass == NDR_FREE)
        return;
    NdrpAlign(c, 3);
    uchar *slots = NdrpTake(c, 4 * dims);
    if (c->Pass == NDR_MARSHAL)
        q->Slots = (ULONG *)slots;
    if (NDR_WIRE_READ(c->Pass))
        for (ULONG i = 0; i < dims; i++)
            q->Conf[i] = ((ULONG *)slots)[i];
}

static void conf_release(NDRC *c, CONF_SAVE *save)
{
    if (!save->Reserved)
        return;
    c->Q->Next = save->Next;
    c->Q->Dims = save->Dims;
    c->Q->Slots = save->Slots;
}

/* ------------------------------------------------------------------ pointer layouts */

static PFORMAT_STRING layout_pointer(NDRC *c, uchar *memory, uchar *wire, PFORMAT_STRING e, LONG_PTR memory_extra,
                                     LONG_PTR wire_extra, const uchar *old)
{
    short mem_off = *(const short *)e;
    short wire_off = *(const short *)(e + 2);
    PFORMAT_STRING pf = e + 4;
    uchar **slot = memory ? (uchar **)(memory + memory_extra + mem_off) : nullptr;
    uchar *wslot = wire ? wire + wire_extra + wire_off : nullptr;
    if (c->Pass == NDR_UNMARSHAL && slot) {
        /* the structure copy put the referent in the slot, so restore the old pointer, or none. */
        c->Q->Preset = old ? *(uchar *const *)(old + memory_extra + mem_off) : nullptr;
        c->Q->HasPreset = TRUE;
    }
    NdrpPointerAt(c, slot, pf, NDRF_EMBEDDED, wslot);
    return pf + 4;
}

/*
 * the pointers a layout lists, with their memory and wire positions and descriptions. when the object was copied whole
 * the wire slot is inside it, so the referent is written there. returns the byte after the layout.
 */
static PFORMAT_STRING pointer_layout(NDRC *c, uchar *memory, uchar *wire, PFORMAT_STRING layout)
{
    PMIDL_STUB_MESSAGE m = c->M;
    PFORMAT_STRING e = layout + 2;
    ULONG_PTR max_count = m->MaxCount;
    const uchar *old = c->Q->Old;
    c->Q->Old = nullptr;
    uchar *saved_memory = m->Memory;
    /* pointee counts may be fields of the enclosing structure. */
    m->Memory = memory;
    while (*e != FC_END) {
        if (*e == FC_NO_REPEAT) {
            e = layout_pointer(c, memory, wire, e + 2, 0, 0, old);
        } else if (*e == FC_FIXED_REPEAT || *e == FC_VARIABLE_REPEAT) {
            BOOL variable = *e == FC_VARIABLE_REPEAT;
            ULONG iterations, increment, array_offset, count;
            if (variable) {
                iterations = (ULONG)max_count;
                increment = *(const USHORT *)(e + 2);
                array_offset = *(const USHORT *)(e + 4);
                count = *(const USHORT *)(e + 6);
                e += 8;
            } else {
                iterations = *(const USHORT *)(e + 2);
                increment = *(const USHORT *)(e + 4);
                array_offset = *(const USHORT *)(e + 6);
                count = *(const USHORT *)(e + 8);
                e += 10;
            }
            PFORMAT_STRING first = e;
            for (ULONG i = 0; i < iterations; i++) {
                PFORMAT_STRING p = first;
                for (ULONG k = 0; k < count; k++)
                    p = layout_pointer(c, memory, wire, p, (LONG_PTR)array_offset + (LONG_PTR)i * increment,
                                       (LONG_PTR)array_offset + (LONG_PTR)i * increment, old);
            }
            e = first + (SIZE_T)count * 8;
        } else {
            NDR_RAISE(RPC_S_INTERNAL_ERROR);
        }
    }
    m->MaxCount = max_count;
    m->Memory = saved_memory;
    return e + 1;
}

/* ------------------------------------------------------------------ whole copies */

/*
 * objects copied whole: writing passes fill the buffer from the object, the reading pass fills the object from the buffer,
 * allocating it first when it has no memory yet.
 */
static void snapshot_pointers(NDRC *c, uchar **pm, SIZE_T bytes, ULONG flags, PFORMAT_STRING layout)
{
    c->Q->Old = nullptr;
    if (c->Pass == NDR_UNMARSHAL && layout && layout[0] == FC_PP && *pm && !(flags & NDRF_MUSTALLOC) && c->M->IsClient) {
        uchar *copy = NdrpScratch(c, bytes);
        memcpy(copy, *pm, bytes);
        c->Q->Old = copy;
    }
}

static uchar *copy_whole(NDRC *c, uchar **pm, ULONG wire_bytes, SIZE_T memory_bytes, ULONG flags, BOOL may_reuse,
                         PFORMAT_STRING layout, uchar **wire_out)
{
    PMIDL_STUB_MESSAGE m = c->M;
    uchar *wire = NdrpTake(c, wire_bytes);
    uchar *mem = nullptr;
    switch (c->Pass) {
    case NDR_MARSHAL: {
        mem = *pm;
        /* clear the last word first so padding at the end does not leak stack. */
        uchar *end = (uchar *)(((ULONG_PTR)wire + wire_bytes + 3) & ~(ULONG_PTR)3);
        if (end - 4 >= wire && (!m->BufferEnd || end <= m->BufferEnd))
            *(ULONG *)(end - 4) = 0;
        memcpy(wire, mem, wire_bytes);
        break;
    }
    case NDR_UNMARSHAL:
        snapshot_pointers(c, pm, wire_bytes, flags, layout);
        if (may_reuse && m->ReuseBuffer && !*pm && !(flags & NDRF_MUSTALLOC)) {
            *pm = wire;
            mem = wire;
            c->Q->PendingRefId = 0;
        } else {
            mem = NdrpAllocObject(c, pm, memory_bytes, flags);
            memcpy(mem, wire, wire_bytes);
        }
        break;
    case NDR_MEMSIZE:
        NdrpAllocObject(c, pm, memory_bytes, flags);
        mem = *pm;
        break;
    default:
        mem = *pm;
    }
    *wire_out = wire;
    return mem;
}

/* ------------------------------------------------------------------ structures */

static void struct_simple(NDRC *c, uchar **pm, PFORMAT_STRING f, ULONG flags)
{
    ULONG size = *(const USHORT *)(f + 2);
    uchar *wire;
    NdrpAlign(c, f[1]);
    uchar *mem = copy_whole(c, pm, size, size, flags, f[0] == FC_STRUCT, f[0] == FC_PSTRUCT ? f + 4 : nullptr, &wire);
    if (f[0] == FC_PSTRUCT && f[4] == FC_PP)
        pointer_layout(c, mem, wire, f + 4);
}

static void struct_conformant(NDRC *c, uchar **pm, PFORMAT_STRING f, ULONG flags)
{
    PMIDL_STUB_MESSAGE m = c->M;
    ULONG fixed = *(const USHORT *)(f + 2);
    PFORMAT_STRING array = f + 4 + *(const short *)(f + 4);
    PFORMAT_STRING layout = f + 6;
    CONF_SAVE save = {};
    ULONG_PTR count;

    if (!(flags & NDRF_CONF_DONE))
        conf_reserve(c, dims_of(m, f, FALSE), &save);
    NdrpAlign(c, f[1]);

    /* the array's own count, already in the reserved slot when unmarshaling. */
    NDR_QUEUE *q = c->Q;
    ULONG my_dim = q->Next;
    if (NDR_WIRE_READ(c->Pass)) {
        count = q->Conf[my_dim];
    } else {
        uchar *base = *pm + fixed;
        count = NdrpComputeConformance(m, base, array + 4);
        if (c->Pass == NDR_MARSHAL && q->Slots)
            q->Slots[my_dim] = (ULONG)count;
    }
    ULONG elem = *(const USHORT *)(array + 2);
    uchar *wire;
    uchar *mem;
    if (f[0] == FC_CVSTRUCT) {
        uchar *fixed_wire;
        mem = copy_whole(c, pm, fixed, fixed + (NDR_WIRE_READ(c->Pass) ? count * elem : 0), flags, FALSE, nullptr,
                         &fixed_wire);
        uchar *saved = m->Memory;
        m->Memory = mem;
        uchar *am = mem ? mem + fixed : nullptr;
        q->Next = my_dim;
        /* a varying tail is a varying array or a string, and carries its own count. */
        if (c->Pass == NDR_MEMSIZE)
            am = NDR_DUMMY;
        NdrpWalk(c, &am, array, flags | NDRF_CONF_DONE);
        m->Memory = saved;
        wire = fixed_wire;
    } else {
        ULONG_PTR body = NdrpCheckedProduct(count, elem);
        ULONG total = fixed + (ULONG)body;
        if (total < fixed)
            NDR_RAISE(RPC_S_INVALID_BOUND);
        mem = copy_whole(c, pm, total, total, flags, f[0] == FC_CSTRUCT, layout, &wire);
        NdrpCheckCount(c, mem + fixed, array + 4, count);
        q->Next = my_dim + 1;
    }
    if (layout[0] == FC_PP) {
        m->MaxCount = count;
        pointer_layout(c, mem, wire, layout);
    }
    conf_release(c, &save);
}

static void struct_bogus_body(NDRC *c, uchar **pm, PFORMAT_STRING f, ULONG flags)
{
    PMIDL_STUB_MESSAGE m = c->M;
    NDR_QUEUE *q = c->Q;
    ULONG size = *(const USHORT *)(f + 2);
    short conf_off = *(const short *)(f + 4);
    PFORMAT_STRING tail = conf_off ? f + 4 + conf_off : nullptr;
    short ptr_off = *(const short *)(f + 6);
    PFORMAT_STRING pointers = ptr_off ? f + 6 + ptr_off : nullptr;
    PFORMAT_STRING member = f + 8;
    CONF_SAVE save = {};

    if (tail && !(flags & NDRF_CONF_DONE))
        conf_reserve(c, dims_of(m, f, FALSE), &save);
    NdrpAlign(c, f[1]);

    uchar *base = nullptr;
    BOOL real = c->Pass != NDR_MEMSIZE;
    if (NDR_WIRE_READ(c->Pass)) {
        ULONG_PTR total = size + (tail ? conf_memory(c, tail, q->Next) : 0);
        if (total > 0xFFFFFFFFu)
            NDR_RAISE(RPC_S_INVALID_BOUND);
        NdrpAllocObject(c, pm, total, flags);
        base = c->Pass == NDR_UNMARSHAL ? *pm : NDR_DUMMY;
    } else {
        base = *pm;
    }
    uchar *saved_memory = m->Memory;
    m->Memory = base;
    uchar *at = base;
    ULONG my_dim = q->Next;
    ULONG_PTR pad = 0;
    (void)pad;

    for (;;) {
        uchar fc = *member;
        if (fc == FC_END)
            break;
        if (fc >= FC_BYTE && fc <= FC_ENUM32 && fc != FC_IGNORE) {
            NdrpSimple(c, at, fc);
            if (real)
                at += NdrSimpleMem[fc];
            member++;
        } else if (fc == FC_INT3264 || fc == FC_UINT3264) {
            NdrpSimple(c, at, fc);
            if (real)
                at += sizeof(INT_PTR);
            member++;
        } else if (fc == FC_IGNORE) {
            NdrpAlign(c, 3);
            uchar *p = NdrpTake(c, 4);
            if (c->Pass == NDR_MARSHAL)
                *(ULONG *)p = 0;
            if (c->Pass == NDR_UNMARSHAL)
                *(ULONG_PTR *)at = 0;
            if (real)
                at += sizeof(void *);
            member++;
        } else if (fc == FC_POINTER) {
            if (!pointers)
                NDR_RAISE(RPC_S_INTERNAL_ERROR);
            NdrpPointerAt(c, (uchar **)at, pointers, NDRF_EMBEDDED, NDR_WIRE_CURSOR);
            pointers += 4;
            if (real)
                at += sizeof(void *);
            member++;
        } else if (fc >= FC_ALIGNM2 && fc <= FC_ALIGNM8) {
            if (real) {
                ULONG_PTR mask = fc == FC_ALIGNM2 ? 1 : fc == FC_ALIGNM4 ? 3 : 7;
                at = base + (((ULONG_PTR)(at - base) + mask) & ~mask);
            }
            member++;
        } else if (fc >= FC_STRUCTPAD1 && fc <= FC_STRUCTPAD7) {
            if (real)
                at += fc - FC_STRUCTPAD1 + 1;
            member++;
        } else if (fc == FC_PAD) {
            member++;
        } else if (fc == FC_EMBEDDED_COMPLEX) {
            uchar *object = real ? at + member[1] : NDR_DUMMY;
            PFORMAT_STRING type = member + 2 + *(const short *)(member + 2);
            if (type[0] == FC_IP || (type[0] >= FC_RP && type[0] <= FC_FP)) {
                NdrpPointerAt(c, (uchar **)object, type, NDRF_EMBEDDED, NDR_WIRE_CURSOR);
                if (real)
                    at = object + sizeof(void *);
            } else {
                uchar *obj = object;
                NdrpWalk(c, &obj, type, type[0] == FC_USER_MARSHAL && (type[1] & USER_MARSHAL_POINTER) ? NDRF_EMBEDDED : 0);
                if (real)
                    at = object + NdrpMemorySize(c, object, type);
            }
            member += 4;
        } else {
            NDR_RAISE(RPC_S_INTERNAL_ERROR);
        }
    }
    if (tail) {
        /* the trailing conformant array is not a member: it follows the fixed part. */
        uchar *object = real ? base + size : NDR_DUMMY;
        q->Next = my_dim;
        NdrpWalk(c, &object, tail, NDRF_CONF_DONE);
    }
    m->Memory = saved_memory;
    conf_release(c, &save);
}

static void struct_bogus(NDRC *c, uchar **pm, PFORMAT_STRING f, ULONG flags)
{
    NdrpRegionEnter(c);
    struct_bogus_body(c, pm, f, flags);
    NdrpRegionLeave(c);
}

void NdrpStruct(NDRC *c, uchar **pm, PFORMAT_STRING f, ULONG flags)
{
    switch (f[0]) {
    case FC_STRUCT:
    case FC_PSTRUCT:
        struct_simple(c, pm, f, flags);
        break;
    case FC_CSTRUCT:
    case FC_CPSTRUCT:
    case FC_CVSTRUCT:
        struct_conformant(c, pm, f, flags);
        break;
    case FC_BOGUS_STRUCT:
        struct_bogus(c, pm, f, flags);
        break;
    default:
        NDR_RAISE(RPC_X_WRONG_STUB_VERSION);
    }
}

/* ------------------------------------------------------------------ arrays */

static void array_fixed(NDRC *c, uchar **pm, PFORMAT_STRING f, ULONG flags)
{
    ULONG total;
    PFORMAT_STRING layout;
    if (f[0] == FC_SMFARRAY) {
        total = *(const USHORT *)(f + 2);
        layout = f + 4;
    } else {
        total = *(const ULONG *)(f + 2);
        layout = f + 6;
    }
    uchar *wire;
    NdrpAlign(c, f[1]);
    uchar *mem = copy_whole(c, pm, total, total, flags, TRUE, layout, &wire);
    if (layout[0] == FC_PP)
        pointer_layout(c, mem, wire, layout);
}

static void array_conformant(NDRC *c, uchar **pm, PFORMAT_STRING f, ULONG flags)
{
    PMIDL_STUB_MESSAGE m = c->M;
    ULONG incr = NDR_DESC_SIZE(m);
    ULONG elem = *(const USHORT *)(f + 2);
    PFORMAT_STRING layout = f + 4 + incr;
    uchar *memory = NDR_WIRE_READ(c->Pass) ? nullptr : *pm;
    ULONG_PTR count = conf_count(c, memory, f + 4, flags, 0);
    ULONG bytes = NdrpCheckedProduct(count, elem);
    uchar *wire;
    NdrpAlign(c, f[1]);
    uchar *mem = copy_whole(c, pm, bytes, bytes, flags, TRUE, layout, &wire);
    NdrpCheckCount(c, mem, f + 4, count);
    if (layout[0] == FC_PP) {
        m->MaxCount = count;
        pointer_layout(c, mem, wire, layout);
    }
}

static void array_conformant_varying(NDRC *c, uchar **pm, PFORMAT_STRING f, ULONG flags)
{
    PMIDL_STUB_MESSAGE m = c->M;
    ULONG incr = NDR_DESC_SIZE(m);
    ULONG elem = *(const USHORT *)(f + 2);
    PFORMAT_STRING variance = f + 4 + incr;
    PFORMAT_STRING layout = f + 4 + 2 * incr;
    uchar *memory = NDR_WIRE_READ(c->Pass) ? nullptr : *pm;
    ULONG_PTR max = conf_count(c, memory, f + 4, flags, 0);
    ULONG offset = 0, actual = 0;
    if (c->Pass == NDR_SIZE || c->Pass == NDR_MARSHAL || c->Pass == NDR_FREE) {
        NdrpComputeVariance(m, memory, variance);
        offset = m->Offset;
        actual = m->ActualCount;
        if (c->Pass != NDR_FREE) {
            NdrpU32(c, offset);
            NdrpU32(c, actual);
        }
    } else {
        offset = NdrpU32(c, 0);
        actual = NdrpU32(c, 0);
    }
    if (offset > max || actual > max || offset + actual > max || offset + actual < offset)
        NDR_RAISE(RPC_S_INVALID_BOUND);
    ULONG bytes = NdrpCheckedProduct(actual, elem);
    uchar *wire = nullptr;
    uchar *mem = nullptr;
    if (actual) {
        NdrpAlign(c, f[1]);
        wire = NdrpTake(c, bytes);
    }
    ULONG_PTR skip = (ULONG_PTR)offset * elem;
    switch (c->Pass) {
    case NDR_MARSHAL:
        mem = *pm;
        if (actual)
            memcpy(wire, mem + skip, bytes);
        break;
    case NDR_UNMARSHAL:
        snapshot_pointers(c, pm, NdrpCheckedProduct(max, elem), flags, layout);
        mem = NdrpAllocObject(c, pm, NdrpCheckedProduct(max, elem), flags);
        if (actual)
            memcpy(mem + skip, wire, bytes);
        break;
    case NDR_MEMSIZE:
        NdrpAllocObject(c, pm, NdrpCheckedProduct(max, elem), flags);
        mem = *pm;
        break;
    default:
        mem = *pm;
    }
    NdrpCheckCount(c, mem, f + 4, max);
    NdrpCheckVariance(c, mem, variance, offset, actual);
    if (layout[0] == FC_PP && actual) {
        m->MaxCount = actual;
        pointer_layout(c, mem, wire, layout);
    }
}

static void array_fixed_varying(NDRC *c, uchar **pm, PFORMAT_STRING f, ULONG flags)
{
    PMIDL_STUB_MESSAGE m = c->M;
    BOOL small = f[0] == FC_SMVARRAY;
    ULONG total, elements, elem;
    PFORMAT_STRING variance;
    if (small) {
        total = *(const USHORT *)(f + 2);
        elements = *(const USHORT *)(f + 4);
        elem = *(const USHORT *)(f + 6);
        variance = f + 8;
    } else {
        total = *(const ULONG *)(f + 2);
        elements = *(const ULONG *)(f + 6);
        elem = *(const USHORT *)(f + 10);
        variance = f + 12;
    }
    PFORMAT_STRING layout = variance + NDR_DESC_SIZE(m);
    uchar *memory = NDR_WIRE_READ(c->Pass) ? nullptr : *pm;
    ULONG offset = 0, actual = 0;
    if (!NDR_WIRE_READ(c->Pass)) {
        NdrpComputeVariance(m, memory, variance);
        offset = m->Offset;
        actual = m->ActualCount;
        if (c->Pass != NDR_FREE) {
            NdrpU32(c, offset);
            NdrpU32(c, actual);
        }
    } else {
        offset = NdrpU32(c, 0);
        actual = NdrpU32(c, 0);
    }
    if (offset > elements || actual > elements || offset + actual > elements)
        NDR_RAISE(RPC_S_INVALID_BOUND);
    ULONG bytes = NdrpCheckedProduct(actual, elem);
    uchar *wire = nullptr;
    uchar *mem;
    if (actual) {
        NdrpAlign(c, f[1]);
        wire = NdrpTake(c, bytes);
    }
    ULONG_PTR skip = (ULONG_PTR)offset * elem;
    switch (c->Pass) {
    case NDR_MARSHAL:
        mem = *pm;
        if (actual)
            memcpy(wire, mem + skip, bytes);
        break;
    case NDR_UNMARSHAL:
        snapshot_pointers(c, pm, total, flags, layout);
        mem = NdrpAllocObject(c, pm, total, flags);
        if (actual)
            memcpy(mem + skip, wire, bytes);
        break;
    case NDR_MEMSIZE:
        NdrpAllocObject(c, pm, total, flags);
        mem = *pm;
        break;
    default:
        mem = *pm;
    }
    NdrpCheckVariance(c, mem, variance, offset, actual);
    if (layout[0] == FC_PP && actual) {
        m->MaxCount = actual;
        pointer_layout(c, mem, wire, layout);
    }
}

/*
 * an array walked element by element. its counts come first as for any conformant type, then the offsets and lengths
 * of the varying dimensions, then the elements.
 */
static void array_bogus_body(NDRC *c, uchar **pm, PFORMAT_STRING f, ULONG flags)
{
    PMIDL_STUB_MESSAGE m = c->M;
    NDR_QUEUE *q = c->Q;
    ULONG incr = NDR_DESC_SIZE(m);
    PFORMAT_STRING conf = f + 4;
    PFORMAT_STRING var = f + 4 + incr;
    PFORMAT_STRING elem = f + 4 + 2 * incr;
    BOOL conformant = *(const ULONG *)conf != 0xFFFFFFFFu;
    BOOL varying = *(const ULONG *)var != 0xFFFFFFFFu;
    CONF_SAVE save = {};
    uchar *memory = NDR_WIRE_READ(c->Pass) ? nullptr : *pm;

    if (conformant && !(flags & NDRF_CONF_DONE))
        conf_reserve(c, dims_of(m, f, FALSE), &save);
    ULONG my_dim = q->Next;
    ULONG_PTR count = *(const USHORT *)(f + 2);
    if (conformant)
        count = conf_count(c, memory, conf, flags | NDRF_CONF_DONE, 0);

    /* per dimension, the elements actually on the wire. */
    ULONG offset = 0, actual = (ULONG)count;
    if (varying) {
        if (!NDR_WIRE_READ(c->Pass)) {
            NdrpComputeVariance(m, memory, var);
            offset = m->Offset;
            actual = m->ActualCount;
            if (c->Pass != NDR_FREE) {
                NdrpU32(c, offset);
                NdrpU32(c, actual);
            }
        } else {
            offset = NdrpU32(c, 0);
            actual = NdrpU32(c, 0);
        }
        if (offset > count || actual > count || offset + actual > count)
            NDR_RAISE(RPC_S_INVALID_BOUND);
    }

    PFORMAT_STRING type = elem;
    BOOL embedded = elem[0] == FC_EMBEDDED_COMPLEX;
    if (embedded)
        type = elem + 2 + *(const short *)(elem + 2);
    uchar etype = type[0];
    ULONG elem_size;
    if (etype == FC_BOGUS_ARRAY || etype == FC_SMFARRAY || etype == FC_LGFARRAY) {
        elem_size = etype == FC_SMFARRAY ? *(const USHORT *)(type + 2)
                    : etype == FC_LGFARRAY ? *(const ULONG *)(type + 2)
                                           : (ULONG)conf_memory(c, type, my_dim + 1);
        if (etype == FC_BOGUS_ARRAY && !*(const ULONG *)(type + 4 + 0) )
            elem_size = (ULONG)conf_memory(c, type, my_dim + 1);
    } else {
        elem_size = NdrpMemorySize(c, nullptr, type);
    }
    if (etype >= FC_C_CSTRING && etype <= FC_WSTRING)
        elem_size = sizeof(void *);

    uchar *base;
    if (NDR_WIRE_READ(c->Pass)) {
        /* every element takes at least a byte, so a count beyond what is left is a lie that would cost a lot of memory. */
        if (m->BufferEnd && actual > (ULONG)(m->BufferEnd - m->Buffer) + 0u && conformant)
            NDR_RAISE(RPC_X_BAD_STUB_DATA);
        NdrpAllocObject(c, pm, NdrpCheckedProduct(count, elem_size), flags);
        base = c->Pass == NDR_UNMARSHAL ? *pm : NDR_DUMMY;
        if (conformant)
            NdrpCheckCount(c, base, conf, count);
        if (varying)
            NdrpCheckVariance(c, base, var, offset, actual);
    } else {
        base = *pm;
    }

    if (actual) {
        NdrpAlign(c, f[1]);
        if (etype >= FC_RP && etype <= FC_FP && !embedded) {
            /* pointers: all the referents first, then their pointees. */
            uchar *at = base + (ULONG_PTR)offset * sizeof(void *);
            for (ULONG i = 0; i < actual; i++, at += sizeof(void *))
                NdrpPointerAt(c, c->Pass == NDR_MEMSIZE ? nullptr : (uchar **)at, elem, NDRF_EMBEDDED,
                              NDR_WIRE_CURSOR);
        } else if (etype == FC_IP && embedded) {
            uchar *at = base + (ULONG_PTR)offset * sizeof(void *);
            for (ULONG i = 0; i < actual; i++, at += sizeof(void *))
                NdrpPointerAt(c, c->Pass == NDR_MEMSIZE ? nullptr : (uchar **)at, type, NDRF_EMBEDDED,
                              NDR_WIRE_CURSOR);
        } else if (!embedded) {
            uchar *at = base + (ULONG_PTR)offset * NdrSimpleMem[elem[0]];
            if (elem[0] == FC_RANGE) {
                for (ULONG i = 0; i < actual; i++, at += NdrSimpleMem[elem[1] & 0xF]) {
                    uchar *item = at;
                    NdrpWalk(c, &item, elem, NDRF_EMBEDDED);
                }
            } else {
                for (ULONG i = 0; i < actual; i++, at += NdrSimpleMem[elem[0]])
                    NdrpSimple(c, at, elem[0]);
            }
        } else {
            uchar *at = base + (ULONG_PTR)offset * elem_size;
            for (ULONG i = 0; i < actual; i++, at += elem_size) {
                uchar *item = at;
                q->Next = my_dim + 1;
                NdrpWalk(c, &item, type,
                         NDRF_CONF_DONE | (type[0] == FC_USER_MARSHAL && (type[1] & USER_MARSHAL_POINTER) ? NDRF_EMBEDDED : 0));
            }
        }
    }
    q->Next = my_dim + 1;
    conf_release(c, &save);
}

static void array_bogus(NDRC *c, uchar **pm, PFORMAT_STRING f, ULONG flags)
{
    NdrpRegionEnter(c);
    array_bogus_body(c, pm, f, flags);
    NdrpRegionLeave(c);
}

void NdrpArray(NDRC *c, uchar **pm, PFORMAT_STRING f, ULONG flags)
{
    switch (f[0]) {
    case FC_SMFARRAY:
    case FC_LGFARRAY:
        array_fixed(c, pm, f, flags);
        break;
    case FC_CARRAY: {
        CONF_SAVE save = {};
        if (!(flags & NDRF_CONF_DONE) && c->Pass == NDR_FREE)
            ;
        array_conformant(c, pm, f, flags);
        conf_release(c, &save);
        break;
    }
    case FC_CVARRAY:
        array_conformant_varying(c, pm, f, flags);
        break;
    case FC_SMVARRAY:
    case FC_LGVARRAY:
        array_fixed_varying(c, pm, f, flags);
        break;
    case FC_BOGUS_ARRAY:
        array_bogus(c, pm, f, flags);
        break;
    default:
        NDR_RAISE(RPC_X_WRONG_STUB_VERSION);
    }
}

/* ------------------------------------------------------------------ strings */

static ULONG string_length(PFORMAT_STRING f, const uchar *s, ULONG unit)
{
    ULONG n = 0;
    if (unit == 2)
        while (((const USHORT *)s)[n])
            n++;
    else if (unit == 1)
        while (s[n])
            n++;
    else
        for (;;) {
            ULONG i;
            for (i = 0; i < unit; i++)
                if (s[(SIZE_T)n * unit + i])
                    break;
            if (i == unit)
                break;
            n++;
        }
    return n + 1;
}

void NdrpString(NDRC *c, uchar **pm, PFORMAT_STRING f, ULONG flags)
{
    PMIDL_STUB_MESSAGE m = c->M;
    uchar fc = f[0];
    BOOL conformant = fc >= FC_C_CSTRING && fc <= FC_C_WSTRING;
    ULONG unit = (fc == FC_C_WSTRING || fc == FC_WSTRING) ? 2 : (fc == FC_C_SSTRING || fc == FC_SSTRING) ? f[1] : 1;
    BOOL sized = conformant && (fc == FC_C_SSTRING ? f[2] : f[1]) == FC_STRING_SIZED;
    PFORMAT_STRING sizedesc = fc == FC_C_SSTRING ? f + 3 : f + 2;
    ULONG max_chars = 0;
    if (!conformant)
        max_chars = *(const USHORT *)(f + (fc == FC_SSTRING ? 3 : 2));
    uchar *memory = NDR_WIRE_READ(c->Pass) ? nullptr : *pm;
    ULONG chars = 0;
    ULONG_PTR max = 0;

    if (!NDR_WIRE_READ(c->Pass) && c->Pass != NDR_FREE) {
        chars = string_length(f, memory, unit);
    }
    if (c->Pass == NDR_FREE)
        return;

    if (conformant) {
        max = conf_count(c, memory, sized ? sizedesc : nullptr, flags, chars);
        if (!NDR_WIRE_READ(c->Pass) && chars > max)
            NDR_RAISE(RPC_S_INVALID_BOUND);
    }
    ULONG offset, actual;
    if (NDR_WIRE_READ(c->Pass)) {
        offset = NdrpU32(c, 0);
        actual = NdrpU32(c, 0);
        if (offset != 0)
            NDR_RAISE(RPC_X_BAD_STUB_DATA);
        if (conformant ? actual > max : actual > max_chars)
            NDR_RAISE(RPC_X_BAD_STUB_DATA);
        if (!actual)
            NDR_RAISE(RPC_X_BAD_STUB_DATA);
    } else {
        if (!conformant && chars > max_chars)
            NDR_RAISE(RPC_S_INVALID_BOUND);
        offset = 0;
        actual = chars;
        NdrpU32(c, 0);
        NdrpU32(c, actual);
    }
    ULONG bytes = NdrpCheckedProduct(actual, unit);
    NdrpAlign(c, unit == 2 ? 1 : unit >= 4 ? 3 : 0);
    uchar *wire = NdrpTake(c, bytes);
    ULONG memory_bytes = conformant ? NdrpCheckedProduct(max, unit) : NdrpCheckedProduct(max_chars, unit);
    switch (c->Pass) {
    case NDR_MARSHAL:
        memcpy(wire, memory, bytes);
        break;
    case NDR_UNMARSHAL: {
        /* the last character must be the terminator. */
        BOOL zero = TRUE;
        for (ULONG i = 0; i < unit; i++)
            if (wire[bytes - unit + i])
                zero = FALSE;
        if (!zero)
            NDR_RAISE(RPC_X_BAD_STUB_DATA);
        if (m->ReuseBuffer && !*pm && !(flags & NDRF_MUSTALLOC) && actual == max && conformant) {
            *pm = wire;
        } else {
            uchar *mem = NdrpAllocObject(c, pm, memory_bytes, flags);
            memcpy(mem, wire, bytes);
        }
        if (sized)
            NdrpCheckCount(c, *pm, sizedesc, max);
        break;
    }
    case NDR_MEMSIZE:
        NdrpAllocObject(c, pm, memory_bytes, flags);
        break;
    default:
        break;
    }
}

/* ------------------------------------------------------------------ unions */

static LONG_PTR switch_value(const uchar *p, uchar type)
{
    switch (type) {
    case FC_BYTE:
    case FC_CHAR:
    case FC_USMALL:
        return *p;
    case FC_SMALL:
        return *(const signed char *)p;
    case FC_SHORT:
        return *(const short *)p;
    case FC_USHORT:
    case FC_WCHAR:
    case FC_ENUM16:
        return *(const USHORT *)p;
    case FC_LONG:
    case FC_ULONG:
    case FC_ENUM32:
        return *(const LONG *)p;
    default:
        NDR_RAISE(RPC_S_INTERNAL_ERROR);
        return 0;
    }
}

/*
 * the switch value, then the arm. each arm of the description is a case value and a type: nothing, a simple type or a
 * type to walk. a value matching no case takes the default arm, or is an error when there is none.
 */
void NdrpUnion(NDRC *c, uchar **pm, PFORMAT_STRING f, ULONG flags)
{
    PMIDL_STUB_MESSAGE m = c->M;
    BOOL encapsulated = f[0] == FC_ENCAPSULATED_UNION;
    uchar sw_type;
    PFORMAT_STRING desc;
    ULONG arm_offset = 0;
    ULONG_PTR memory_size;
    uchar *mem = nullptr;
    LONG_PTR sw = 0;

    PFORMAT_STRING desc_switch = f + 2;
    if (encapsulated) {
        sw_type = f[1] & 0x0F;
        arm_offset = f[1] >> 4;
        desc = f + 2;
    } else {
        sw_type = f[1];
        PFORMAT_STRING d = f + 2;
        desc = d + NDR_DESC_SIZE(m);
        desc += *(const short *)desc;
        /* only the passes that have the union's memory know the switch here. */
        if (!NDR_WIRE_READ(c->Pass))
            sw = (LONG_PTR)NdrpComputeConformance(m, *pm, d);
    }
    memory_size = *(const USHORT *)desc;
    USHORT info = *(const USHORT *)(desc + 2);
    ULONG align_mask = info >> 12;
    ULONG arms = info & 0xFFF;

    if (NDR_WIRE_READ(c->Pass)) {
        /* the format's size covers the arms, an encapsulated union has its switch before them. */
        mem = NdrpAllocObject(c, pm, memory_size + arm_offset, flags);
    } else {
        mem = *pm;
    }
    if (encapsulated && !NDR_WIRE_READ(c->Pass))
        sw = switch_value(mem, sw_type);

    uchar *wire_value;
    NdrpAlign(c, NdrSimpleAlign[sw_type]);
    wire_value = NdrpTake(c, NdrSimpleWire[sw_type]);
    if (c->Pass == NDR_MARSHAL) {
        memcpy(wire_value, encapsulated ? mem : (uchar *)&sw, NdrSimpleWire[sw_type]);
        if (!encapsulated && NdrSimpleWire[sw_type] == 1)
            *wire_value = (uchar)sw;
    } else if (c->Pass == NDR_UNMARSHAL) {
        sw = switch_value(wire_value, sw_type);
        if (encapsulated)
            memcpy(mem, wire_value, NdrSimpleWire[sw_type]);
        else
            NdrpCheckSwitch(c, mem, desc_switch, sw);
    } else if (c->Pass == NDR_MEMSIZE) {
        sw = switch_value(wire_value, sw_type);
    }
    NdrpAlign(c, align_mask);

    const uchar *e = desc + 4;
    ULONG i;
    for (i = 0; i < arms; i++, e += 6)
        if (*(const LONG *)e == (LONG)sw)
            break;
    short arm = *(const short *)(i < arms ? e + 4 : e);
    if (i == arms && (USHORT)arm == 0xFFFF)
        NDR_RAISE(RPC_S_INVALID_TAG);
    if (!arm)
        return;
    uchar *arm_memory = mem ? mem + arm_offset : NDR_DUMMY;
    if ((arm & 0xFF00) == 0x8000) {
        NdrpSimple(c, arm_memory, (uchar)arm);
        return;
    }
    const uchar *target = (i < arms ? e + 4 : e) + arm;
    if (target[0] >= FC_RP && target[0] <= FC_FP)
        NdrpPointerAt(c, (uchar **)arm_memory, target, NDRF_EMBEDDED, NDR_WIRE_CURSOR);
    else if (target[0] == FC_IP)
        NdrpPointerAt(c, (uchar **)arm_memory, target, NDRF_EMBEDDED, NDR_WIRE_CURSOR);
    else {
        uchar *obj = arm_memory;
        NdrpWalk(c, &obj, target, 0);
    }
}
