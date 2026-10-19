/*
 * the walker of the ndr engine: the wire cursor of each pass, the queue of deferred pointees, simple types, pointers,
 * correlation, and the dispatch by format character. The aggregates are in ndr_type.c.
 */
#include "ndr.h"

/* ------------------------------------------------------------------ the wire cursor */

/*
 * aligns the position of the pass to mask + 1 bytes. Sizing counts in BufferLength, the others move Buffer, and marshaling
 * zeroes what it skips so that nothing of the stack leaks.
 */
void NdrpAlign(NDRC *c, ULONG mask)
{
    PMIDL_STUB_MESSAGE m = c->M;
    if (c->Pass == NDR_SIZE) {
        ULONG length = m->BufferLength;
        ULONG aligned = length + mask;
        if (aligned < length)
            NDR_RAISE(RPC_S_INVALID_BOUND);
        m->BufferLength = aligned & ~mask;
        return;
    }
    if (c->Pass == NDR_FREE)
        return;
    uchar *buffer = m->Buffer;
    uchar *aligned = (uchar *)(((ULONG_PTR)buffer + mask) & ~(ULONG_PTR)mask);
    if (aligned != buffer) {
        if (m->BufferEnd && aligned > m->BufferEnd)
            NDR_RAISE(RPC_X_BAD_STUB_DATA);
        if (c->Pass == NDR_MARSHAL)
            memset(buffer, 0, (SIZE_T)(aligned - buffer));
        m->Buffer = aligned;
    }
}

/* takes count bytes of the wire; sizing and freeing have none to give. */
uchar *NdrpTake(NDRC *c, ULONG count)
{
    PMIDL_STUB_MESSAGE m = c->M;
    if (c->Pass == NDR_SIZE) {
        ULONG length = m->BufferLength + count;
        if (length < count || length < m->BufferLength)
            NDR_RAISE(RPC_S_INVALID_BOUND);
        m->BufferLength = length;
        return nullptr;
    }
    if (c->Pass == NDR_FREE)
        return nullptr;
    uchar *buffer = m->Buffer;
    if (m->BufferEnd && (buffer > m->BufferEnd || (SIZE_T)(m->BufferEnd - buffer) < count))
        NDR_RAISE(RPC_X_BAD_STUB_DATA);
    m->Buffer = buffer + count;
    return buffer;
}

/* a 32 bit number: written by marshaling, read by the passes that read, counted by sizing. */
ULONG NdrpU32(NDRC *c, ULONG value)
{
    if (c->Pass == NDR_FREE)
        return value;
    NdrpAlign(c, 3);
    uchar *p = NdrpTake(c, 4);
    switch (c->Pass) {
    case NDR_MARSHAL:
        *(ULONG *)p = value;
        return value;
    case NDR_UNMARSHAL:
    case NDR_MEMSIZE:
        return *(ULONG *)p;
    default:
        return value;
    }
}

/* the product of a count and a size, as a number of bytes that fits the wire's 32 bits. */
ULONG NdrpCheckedProduct(ULONG_PTR a, ULONG_PTR b)
{
    ULONGLONG product = (ULONGLONG)a * b;
    if (product > 0xFFFFFFFFull)
        NDR_RAISE(RPC_S_INVALID_BOUND);
    return (ULONG)product;
}

/* ------------------------------------------------------------------ simple types */

/* one value of a simple type at memory; enums are shortened on the wire and checked going in and out. */
void NdrpSimple(NDRC *c, uchar *memory, uchar fc)
{
    ULONG wire = NdrSimpleWire[fc];
    if (!wire) {
        if (fc == FC_ZERO || fc == FC_PAD)
            return;
        NDR_RAISE(RPC_S_INTERNAL_ERROR);
    }
    if (c->Pass == NDR_FREE)
        return;
    NdrpAlign(c, NdrSimpleAlign[fc]);
    uchar *p = NdrpTake(c, wire);
    switch (c->Pass) {
    case NDR_MARSHAL:
        switch (fc) {
        case FC_ENUM16: {
            ULONG v = *(ULONG *)memory;
            if (v & 0xFFFF8000u)
                NDR_RAISE(RPC_X_ENUM_VALUE_OUT_OF_RANGE);
            *(USHORT *)p = (USHORT)v;
            break;
        }
        case FC_INT3264:
        case FC_UINT3264:
            *(ULONG *)p = (ULONG) * (ULONG_PTR *)memory;
            break;
        case FC_IGNORE:
            *(ULONG *)p = 0;
            break;
        default:
            memcpy(p, memory, wire);
        }
        break;
    case NDR_UNMARSHAL:
        switch (fc) {
        case FC_ENUM16: {
            ULONG v = *(USHORT *)p;
            if (v & 0x8000)
                NDR_RAISE(RPC_X_ENUM_VALUE_OUT_OF_RANGE);
            *(ULONG *)memory = v;
            break;
        }
        case FC_INT3264:
            *(INT_PTR *)memory = *(LONG *)p;
            break;
        case FC_UINT3264:
            *(ULONG_PTR *)memory = *(ULONG *)p;
            break;
        case FC_IGNORE:
            *(ULONG_PTR *)memory = 0;
            break;
        default:
            memcpy(memory, p, wire);
        }
        break;
    default:
        break;
    }
}

/* ------------------------------------------------------------------ the queue of pointees */

static void queue_init(NDR_QUEUE *q, NDR_PASS pass)
{
    memset(q, 0, sizeof(*q));
    q->Pass = pass;
    q->Chunks = &q->First;
}

static void queue_free(NDR_QUEUE *q)
{
    NDR_QCHUNK *chunk = q->First.Next;
    while (chunk) {
        NDR_QCHUNK *next = chunk->Next;
        HeapFree(GetProcessHeap(), 0, chunk);
        chunk = next;
    }
    q->First.Next = nullptr;
}

static NDR_QITEM *queue_new(NDR_QUEUE *q)
{
    ULONG per = sizeof(q->First.Items) / sizeof(q->First.Items[0]);
    if (q->Used == per) {
        NDR_QCHUNK *chunk = HeapAlloc(GetProcessHeap(), 0, sizeof(*chunk));
        if (!chunk)
            NDR_RAISE(RPC_S_OUT_OF_MEMORY);
        chunk->Next = q->First.Next;
        q->First.Next = chunk;
        q->Chunks = chunk;
        q->Used = 0;
    }
    return &q->Chunks->Items[q->Used++];
}

static void queue_push(NDRC *c, uchar **slot, uchar *pointer, PFORMAT_STRING format, ULONG refid)
{
    NDR_QUEUE *q = c->Q;
    NDR_QITEM *it = queue_new(q);
    it->Next = nullptr;
    it->Slot = slot;
    it->Pointer = pointer;
    it->Format = format;
    it->RefId = refid;
    if (q->Tail)
        q->Tail->Next = it;
    else
        q->Head = it;
    q->Tail = it;
}

static void pointee_walk(NDRC *c, uchar **slot, uchar *pointer, PFORMAT_STRING pf, ULONG refid);

/* the pointees run first in, first out, and the ones they queue run after them. */
static void queue_run(NDRC *c)
{
    NDR_QUEUE *q = c->Q;
    while (q->Head) {
        NDR_QITEM item = *q->Head;
        q->Head = item.Next;
        if (!q->Head)
            q->Tail = nullptr;
        pointee_walk(c, item.Slot, item.Pointer, item.Format, item.RefId);
    }
}

/* ------------------------------------------------------------------ allocation during unmarshaling */

/*
 * the memory of an object an unmarshaling fills: the caller's when it has one and does not want a new one, else new. A full
 * pointer's pointee is filed under its id before its content is read, so that a pointer into itself finds it. The pass
 * that only measures counts what would be allocated.
 */
uchar *NdrpAllocObject(NDRC *c, uchar **memory, SIZE_T size, ULONG flags)
{
    PMIDL_STUB_MESSAGE m = c->M;
    if (c->Pass == NDR_MEMSIZE) {
        if (!*memory || (flags & NDRF_MUSTALLOC))
            m->MemorySize = ((m->MemorySize + 7) & ~7u) + (ULONG)size;
        if (!*memory)
            *memory = NDR_DUMMY;
        c->Q->PendingRefId = 0;
        return *memory;
    }
    if (c->Pass != NDR_UNMARSHAL)
        return *memory;
    if (!*memory || (flags & NDRF_MUSTALLOC)) {
        SIZE_T n = size ? size : 1;
        *memory = NdrpAlloc(m, n);
        memset(*memory, 0, n);
    }
    if (c->Q->PendingRefId) {
        ULONG id = c->Q->PendingRefId;
        c->Q->PendingRefId = 0;
        NdrFullPointerInsertRefId(m->FullPtrXlatTables, id, *memory);
    }
    return *memory;
}

/* ------------------------------------------------------------------ pointers */

/*
 * the interface pointer hooks of ole32, set when com is in the process: sizing and marshaling an interface pointer into
 * the buffer, unmarshaling it, and releasing it.
 */
typedef struct {
    void (*Size)(PMIDL_STUB_MESSAGE m, uchar *pointer, PFORMAT_STRING format);
    void (*Marshal)(PMIDL_STUB_MESSAGE m, uchar *pointer, PFORMAT_STRING format);
    void (*Unmarshal)(PMIDL_STUB_MESSAGE m, uchar **pointer, PFORMAT_STRING format);
    void (*Free)(PMIDL_STUB_MESSAGE m, uchar *pointer, PFORMAT_STRING format);
    void (*Skip)(PMIDL_STUB_MESSAGE m);
} NDR_OLE_HOOKS;

extern const NDR_OLE_HOOKS *NdrpOleHooks(void);

static BOOL may_free(PMIDL_STUB_MESSAGE m, uchar type, uchar attrs, uchar *pointer)
{
    if (attrs & (FC_DONT_FREE | FC_ALLOCED_ON_STACK))
        return FALSE;
    if (NdrpInBuffer(m, pointer))
        return FALSE;
    if (type == FC_FP && m->FullPtrXlatTables)
        return NdrFullPointerFree(m->FullPtrXlatTables, pointer) != 0;
    return TRUE;
}

/*
 * walks the object a pointer points to. For the passes that read memory, pointer is the pointee; unmarshaling puts the pointee
 * in *slot.
 */
static void pointee_walk(NDRC *c, uchar **slot, uchar *pointer, PFORMAT_STRING pf, ULONG refid)
{
    uchar type = pf[0];
    uchar attrs = pf[1];
    PMIDL_STUB_MESSAGE m = c->M;

    if (type == FC_IP) {
        const NDR_OLE_HOOKS *hooks = NdrpOleHooks();
        if (!hooks)
            NDR_RAISE(RPC_E_UNEXPECTED);
        switch (c->Pass) {
        case NDR_SIZE:
            hooks->Size(m, pointer, pf);
            break;
        case NDR_MARSHAL:
            hooks->Marshal(m, pointer, pf);
            break;
        case NDR_UNMARSHAL:
            hooks->Unmarshal(m, slot, pf);
            break;
        case NDR_MEMSIZE:
            hooks->Skip(m);
            break;
        case NDR_FREE:
            hooks->Free(m, pointer, pf);
            break;
        }
        return;
    }

    if (++c->Q->Depth > 4096)
        NDR_RAISE(RPC_X_BAD_STUB_DATA);
    if (c->Pass == NDR_UNMARSHAL)
        c->Q->PendingRefId = type == FC_FP ? refid : 0;

    if (attrs & FC_SIMPLE_POINTER) {
        uchar fc = pf[2];
        if (NDR_WIRE_READ(c->Pass)) {
            uchar *object = c->Pass == NDR_UNMARSHAL ? *slot : nullptr;
            NdrpAllocObject(c, &object, NdrSimpleMem[fc], 0);
            NdrpSimple(c, object, fc);
            if (c->Pass == NDR_UNMARSHAL)
                *slot = object;
        } else {
            NdrpSimple(c, pointer, fc);
            if (c->Pass == NDR_FREE && may_free(m, type, attrs, pointer))
                NdrpFree(m, pointer);
        }
        c->Q->Depth--;
        return;
    }

    PFORMAT_STRING pointee = pf + 2 + *(const short *)(pf + 2);
    if (NDR_WIRE_READ(c->Pass)) {
        uchar *object = c->Pass == NDR_UNMARSHAL ? *slot : nullptr;
        if (attrs & FC_POINTER_DEREF) {
            /* the pointee is a pointer itself, in a cell of its own. */
            NdrpAllocObject(c, &object, sizeof(void *), 0);
            c->Q->PendingRefId = 0;
            uchar *dummy = nullptr;
            NdrpWalk(c, c->Pass == NDR_UNMARSHAL ? (uchar **)object : &dummy, pointee, 0);
        } else {
            NdrpWalk(c, &object, pointee, 0);
        }
        if (c->Pass == NDR_UNMARSHAL)
            *slot = object;
    } else {
        uchar *object = pointer;
        if (attrs & FC_POINTER_DEREF)
            NdrpWalk(c, (uchar **)object, pointee, 0);
        else
            NdrpWalk(c, &object, pointee, 0);
        if (c->Pass == NDR_FREE && may_free(m, type, attrs, pointer))
            NdrpFree(m, pointer);
    }
    c->Q->Depth--;
}

/* the referent of a pointer: written, read or counted where the caller says. */
static ULONG referent_io(NDRC *c, uchar *wire, ULONG value)
{
    if (wire == NDR_WIRE_CURSOR)
        return NdrpU32(c, value);
    if (!wire)
        return value;
    if (c->Pass == NDR_MARSHAL) {
        *(ULONG *)wire = value;
        return value;
    }
    if (NDR_WIRE_READ(c->Pass))
        return *(ULONG *)wire;
    return value;
}

/*
 * a pointer of any kind at *slot. Its referent goes on the wire where the pointer stands and its pointee is written at once
 * for a top level pointer and queued for one inside another object.
 */
void NdrpPointerAt(NDRC *c, uchar **slot, PFORMAT_STRING pf, ULONG flags, uchar *wire)
{
    uchar type = pf[0];
    PMIDL_STUB_MESSAGE m = c->M;
    uchar *pointer = c->Pass == NDR_MEMSIZE ? nullptr : *slot;
    ULONG refid = 0;
    BOOL present;

    switch (c->Pass) {
    case NDR_SIZE:
    case NDR_MARSHAL:
        switch (type) {
        case FC_RP:
            if (!pointer)
                NDR_RAISE(RPC_X_NULL_REF_POINTER);
            present = TRUE;
            break;
        case FC_UP:
        case FC_OP:
        case FC_IP:
            present = pointer != nullptr;
            referent_io(c, wire, present ? 0x20000 + 4 * m->UniquePtrCount++ : 0);
            break;
        case FC_FP:
            present = !NdrFullPointerQueryPointer(m->FullPtrXlatTables, pointer, c->Pass == NDR_SIZE ? 4 : 1, &refid);
            referent_io(c, wire, refid);
            break;
        default:
            NDR_RAISE(RPC_S_INTERNAL_ERROR);
            return;
        }
        break;
    case NDR_UNMARSHAL:
    case NDR_MEMSIZE:
        if (type == FC_RP) {
            present = TRUE;
        } else {
            refid = referent_io(c, wire, 0);
            if (type == FC_FP) {
                void *existing = nullptr;
                present = !NdrFullPointerQueryRefId(m->FullPtrXlatTables, refid, c->Pass == NDR_MEMSIZE ? 8 : 2,
                                                    &existing);
                if (!present && c->Pass == NDR_UNMARSHAL)
                    *slot = existing;
            } else {
                present = refid != 0;
                if (!present && c->Pass == NDR_UNMARSHAL)
                    *slot = nullptr;
            }
        }
        break;
    case NDR_FREE:
        present = pointer != nullptr;
        break;
    default:
        return;
    }
    if (!present)
        return;

    if ((flags & NDRF_EMBEDDED) && m->IgnoreEmbeddedPointers)
        return;
    if (flags & NDRF_EMBEDDED)
        queue_push(c, slot, pointer, pf, refid);
    else
        pointee_walk(c, slot, pointer, pf, refid);
}

/* ------------------------------------------------------------------ correlation */

static ULONG_PTR read_integer(const uchar *p, uchar fc)
{
    switch (fc) {
    case FC_SMALL:
        return (ULONG_PTR)(LONG_PTR) * (const signed char *)p;
    case FC_USMALL:
    case FC_BYTE:
    case FC_CHAR:
        return *p;
    case FC_SHORT:
        return (ULONG_PTR)(LONG_PTR) * (const short *)p;
    case FC_USHORT:
    case FC_ENUM16:
    case FC_WCHAR:
        return *(const USHORT *)p;
    case FC_LONG:
        return (ULONG_PTR)(LONG_PTR) * (const LONG *)p;
    case FC_ULONG:
    case FC_ENUM32:
    case FC_ERROR_STATUS_T:
        return *(const ULONG *)p;
    case FC_HYPER:
        return (ULONG_PTR) * (const ULONGLONG *)p;
    default:
        NDR_RAISE(RPC_S_INTERNAL_ERROR);
        return 0;
    }
}

/*
 * the value a correlation descriptor names: a field next to the array, one the array's structure holds, a parameter, a
 * constant, or what a routine of the stub computes. The descriptor is a type and operator byte and a 16 bit offset; a
 * descriptor of the new kind has more after it, which is not needed to find the value.
 */
ULONG_PTR NdrpComputeConformance(PMIDL_STUB_MESSAGE m, uchar *memory, PFORMAT_STRING d)
{
    uchar kind = d[0] & 0xF0;
    uchar op = d[1];
    uchar fc = d[0] & 0x0F;
    ULONG_PTR value;
    const uchar *address;

    if (op == FC_CALLBACK) {
        uchar *stack = m->StackTop;
        if (kind != FC_TOP_LEVEL_CONFORMANCE)
            m->StackTop = kind == FC_POINTER_CONFORMANCE ? m->Memory : memory;
        m->StubDesc->apfnExprEval[*(const USHORT *)(d + 2)](m);
        m->StackTop = stack;
        return m->MaxCount;
    }

    switch (kind) {
    case 0:
        address = memory + *(const short *)(d + 2);
        break;
    case FC_POINTER_CONFORMANCE:
        address = m->Memory + *(const short *)(d + 2);
        break;
    case FC_TOP_LEVEL_CONFORMANCE:
        if (!m->StackTop)
            return m->MaxCount;
        address = m->StackTop + *(const USHORT *)(d + 2);
        break;
    case FC_CONSTANT_CONFORMANCE:
        value = *(const USHORT *)(d + 2) | ((ULONG_PTR)d[1] << 16);
        m->MaxCount = (ULONG)value;
        return value;
    case FC_TOP_LEVEL_MULTID_CONFORMANCE: {
        PARRAY_INFO info = m->pArrayInfo;
        value = info ? info->MaxCountArray[info->Dimension] : m->SizePtrCountArray[*(const USHORT *)(d + 2)];
        m->MaxCount = value;
        return value;
    }
    default:
        NDR_RAISE(RPC_S_INTERNAL_ERROR);
        return 0;
    }
    if (op == FC_DEREFERENCE)
        address = *(const uchar *const *)address;
    value = read_integer(address, fc);
    switch (op) {
    case FC_DIV_2:
        value = (ULONG_PTR)((LONG_PTR)value / 2);
        break;
    case FC_MULT_2:
        value = (ULONG_PTR)((LONG_PTR)value * 2);
        break;
    case FC_ADD_1:
        value++;
        break;
    case FC_SUB_1:
        value--;
        break;
    default:
        break;
    }
    m->MaxCount = (ULONG)value;
    return value;
}

/* the variance descriptor is read the same way, and gives the actual count. */
ULONG_PTR NdrpComputeVariance(PMIDL_STUB_MESSAGE m, uchar *memory, PFORMAT_STRING d)
{
    ULONG_PTR max = m->MaxCount;
    ULONG_PTR value = NdrpComputeConformance(m, memory, d);
    m->MaxCount = max;
    m->ActualCount = (ULONG)value;
    m->Offset = 0;
    return value;
}

/* ------------------------------------------------------------------ dispatch */

/* walks the object of format f whose memory is *memory (for a pointer type, the pointer itself). */
void NdrpWalk(NDRC *c, uchar **memory, PFORMAT_STRING f, ULONG flags)
{
    uchar fc = f[0];
    switch (fc) {
    case FC_RP:
    case FC_UP:
    case FC_OP:
    case FC_FP:
    case FC_IP:
        NdrpPointerAt(c, memory, f, flags & ~NDRF_EMBEDDED ? flags : flags, NDR_WIRE_CURSOR);
        break;
    case FC_STRUCT:
    case FC_PSTRUCT:
    case FC_CSTRUCT:
    case FC_CPSTRUCT:
    case FC_CVSTRUCT:
    case FC_BOGUS_STRUCT:
        NdrpStruct(c, memory, f, flags);
        break;
    case FC_CARRAY:
    case FC_CVARRAY:
    case FC_SMFARRAY:
    case FC_LGFARRAY:
    case FC_SMVARRAY:
    case FC_LGVARRAY:
    case FC_BOGUS_ARRAY:
        NdrpArray(c, memory, f, flags);
        break;
    case FC_C_CSTRING:
    case FC_C_BSTRING:
    case FC_C_SSTRING:
    case FC_C_WSTRING:
    case FC_CSTRING:
    case FC_BSTRING:
    case FC_SSTRING:
    case FC_WSTRING:
        NdrpString(c, memory, f, flags);
        break;
    case FC_ENCAPSULATED_UNION:
    case FC_NON_ENCAPSULATED_UNION:
        NdrpUnion(c, memory, f, flags);
        break;
    case FC_BYTE_COUNT_POINTER:
    case FC_TRANSMIT_AS:
    case FC_REPRESENT_AS:
    case FC_USER_MARSHAL:
    case FC_RANGE:
    case FC_BLKHOLE:
    case FC_BIND_CONTEXT:
        NdrpSpecial(c, memory, f, flags);
        break;
    default:
        if (NdrSimpleWire[fc]) {
            /* a simple type given by its character. */
            NdrpAllocObject(c, memory, NdrSimpleMem[fc], flags);
            NdrpSimple(c, *memory, fc);
            break;
        }
        NDR_RAISE(RPC_S_INTERNAL_ERROR);
    }
}

/*
 * the outermost walk of a construct: creates the queue when there is none, runs it when the walk is done, and removes it
 * even when an exception leaves. A walk inside another, which is what a stub does when it hands a part of an object to
 * a function of the engine, uses the queue of the first.
 */
void NdrpWalkTop(PMIDL_STUB_MESSAGE m, NDR_PASS pass, uchar **memory, PFORMAT_STRING format, ULONG flags)
{
    NDR_QUEUE *existing = m->pPointerQueueState;
    if (existing) {
        NDRC c = {m, pass, existing};
        NdrpWalk(&c, memory, format, flags);
        return;
    }
    NDR_QUEUE queue;
    queue_init(&queue, pass);
    m->pPointerQueueState = &queue;
    __try {
        NDRC c = {m, pass, &queue};
        NdrpWalk(&c, memory, format, flags);
        queue_run(&c);
    }
    __finally {
        m->pPointerQueueState = nullptr;
        queue_free(&queue);
    }
}
