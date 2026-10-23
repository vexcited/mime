/*
 * the ndr walker: the wire cursor of each pass, the queue of deferred pointees, simple types, pointers, correlation and
 * the dispatch by format character. aggregates are in ndr_type.c.
 */
#include "ndr.h"
#include "ndr_proc.h"

/* ------------------------------------------------------------------ wire cursor */

/*
 * aligns to mask + 1 bytes. sizing counts in BufferLength, the other passes move Buffer, and marshaling zeroes what it
 * skips so no stack leaks.
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

/* sizing and freeing have no wire bytes to give. */
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

/* written by marshaling, read by the reading passes, counted by sizing. */
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

/* count times size, which must fit the wire's 32 bits. */
ULONG NdrpCheckedProduct(ULONG_PTR a, ULONG_PTR b)
{
    ULONGLONG product = (ULONGLONG)a * b;
    if (product > 0xFFFFFFFFull)
        NDR_RAISE(RPC_S_INVALID_BOUND);
    return (ULONG)product;
}

/* ------------------------------------------------------------------ simple types */

/* enums are shortened on the wire and range checked both ways. */
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

/* ------------------------------------------------------------------ pointee queue */

static void queue_init(NDR_QUEUE *q, NDR_PASS pass)
{
    memset(q, 0, sizeof(*q));
    q->Pass = pass;
    q->Chunks = &q->First;
}

typedef struct NDR_SCRATCH {
    struct NDR_SCRATCH *Next;
} NDR_SCRATCH;

/* freed with the walk, even when an exception leaves it. */
void *NdrpScratch(NDRC *c, SIZE_T size)
{
    NDR_SCRATCH *block = HeapAlloc(GetProcessHeap(), 0, sizeof(*block) + size + 16);
    if (!block)
        NDR_RAISE(RPC_S_OUT_OF_MEMORY);
    block->Next = c->Q->Scratch;
    c->Q->Scratch = block;
    return (uchar *)block + 16;
}

static void queue_free(NDR_QUEUE *q)
{
    while (q->Scratch) {
        NDR_SCRATCH *next = q->Scratch->Next;
        HeapFree(GetProcessHeap(), 0, q->Scratch);
        q->Scratch = next;
    }
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
    it->Memory = c->M->Memory;
    if (q->Tail)
        q->Tail->Next = it;
    else
        q->Head = it;
    q->Tail = it;
}

static void pointee_walk(NDRC *c, uchar **slot, uchar *pointer, PFORMAT_STRING pf, ULONG refid);

/* the pointee of a user type with a pointer wire form is queued behind its enclosing object: the object is the pointer. */
void NdrpQueueUser(NDRC *c, uchar *object, PFORMAT_STRING f)
{
    queue_push(c, nullptr, object, f, 0);
}

/* first in, first out, and pointees queued by pointees run after them. */
static void queue_run(NDRC *c)
{
    NDR_QUEUE *q = c->Q;
    /* a pointee's own queued pointees go behind those queued before, so one loop does them all. */
    if (q->Draining)
        return;
    q->Draining = TRUE;
    while (q->Head) {
        NDR_QITEM item = *q->Head;
        q->Head = item.Next;
        if (!q->Head)
            q->Tail = nullptr;
        uchar *saved = c->M->Memory;
        c->M->Memory = item.Memory;
        pointee_walk(c, item.Slot, item.Pointer, item.Format, item.RefId);
        c->M->Memory = saved;
    }
    q->Draining = FALSE;
}

/* ------------------------------------------------------------------ unmarshaling allocation */

/*
 * uses the caller's memory when there is some and no new memory is required, else allocates. a full pointer's pointee is
 * filed under its id before its content is read, so a pointer into itself finds it. the measuring pass only counts.
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
        /* cap what a wire count can make the stub allocate, so a lie is an error and not an outage. */
        if (size > NDR_MAX_ALLOC)
            NDR_RAISE(RPC_X_BAD_STUB_DATA);
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

static BOOL may_free(PMIDL_STUB_MESSAGE m, uchar type, uchar attrs, uchar *pointer)
{
    if (attrs & (FC_DONT_FREE | FC_ALLOCED_ON_STACK))
        return FALSE;
    return !NdrpInBuffer(m, pointer);
}

/* for the passes that read memory, pointer is the pointee. unmarshaling puts the pointee in *slot. */
static void pointee_walk(NDRC *c, uchar **slot, uchar *pointer, PFORMAT_STRING pf, ULONG refid)
{
    uchar type = pf[0];
    uchar attrs = pf[1];
    PMIDL_STUB_MESSAGE m = c->M;

    if (type == FC_USER_MARSHAL) {
        NdrpUserPointee(c, pointer, pf);
        return;
    }
    if (type == FC_IP) {
        const NDR_OLE_HOOKS *hooks = NdrpOleHooks();
        if (!hooks)
            NDR_RAISE(RPC_E_UNEXPECTED);
        switch (c->Pass) {
        case NDR_SIZE:
            hooks->Size(c, pointer, pf);
            break;
        case NDR_MARSHAL:
            hooks->Marshal(c, pointer, pf);
            break;
        case NDR_UNMARSHAL:
            hooks->Unmarshal(c, slot, pf);
            break;
        case NDR_MEMSIZE:
            hooks->Skip(c);
            break;
        case NDR_FREE:
            hooks->Free(c, pointer, pf);
            break;
        }
        return;
    }

    if (++c->Q->Depth > 4096)
        NDR_RAISE(RPC_X_BAD_STUB_DATA);
    if (NDR_WIRE_READ(c->Pass) && type == FC_FP) {
        /* a ref id seen before names an object already read, which the pointer then points to. */
        void *existing = nullptr;
        if (NdrFullPointerQueryRefId(m->FullPtrXlatTables, refid, c->Pass == NDR_MEMSIZE ? 8 : 2, &existing)) {
            if (c->Pass == NDR_UNMARSHAL)
                *slot = existing;
            c->Q->Depth--;
            return;
        }
    }
    if (c->Pass == NDR_UNMARSHAL)
        c->Q->PendingRefId = type == FC_FP ? refid : 0;

    /* a simple pointer describes its pointee inline: a base type or a string. */
    PFORMAT_STRING pointee = (attrs & FC_SIMPLE_POINTER) ? pf + 2 : pf + 2 + *(const short *)(pf + 2);
    if (NDR_WIRE_READ(c->Pass)) {
        uchar *object = c->Pass == NDR_UNMARSHAL ? *slot : nullptr;
        if (attrs & FC_POINTER_DEREF) {
            /* the pointee is itself a pointer, in its own cell. */
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
 * the referent goes on the wire where the pointer stands. a top level pointer's pointee is written at once, an embedded
 * one's is queued.
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
            /* only embedded pointers get a referent number, never zero. parameters have none. */
            if (flags & NDRF_EMBEDDED)
                referent_io(c, wire, 0x20000 + 4 * (c->Pass == NDR_MARSHAL ? m->UniquePtrCount++ : m->UniquePtrCount));
            break;
        case FC_UP:
        case FC_OP:
        case FC_IP:
            present = pointer != nullptr;
            /* only marshaling advances the counter, sizing just reads it. */
            referent_io(c, wire, present ? 0x20000 + 4 * (c->Pass == NDR_MARSHAL ? m->UniquePtrCount++ : m->UniquePtrCount) : 0);
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
            if (flags & NDRF_EMBEDDED)
                referent_io(c, wire, 0);
            if (c->Q->HasPreset && slot)
                *slot = c->Q->Preset;
            c->Q->HasPreset = FALSE;
        } else {
            refid = referent_io(c, wire, 0);
            if (c->Q->HasPreset && slot)
                *slot = c->Q->Preset;
            c->Q->HasPreset = FALSE;
            /* the client releases the interface a pointer held before the reply replaces it. */
            if (type == FC_IP && c->Pass == NDR_UNMARSHAL && m->IsClient && slot && *slot) {
                const NDR_OLE_HOOKS *hooks = NdrpOleHooks();
                if (hooks)
                    hooks->Free(c, *slot, pf);
                *slot = nullptr;
            }
            present = refid != 0;
            if (!present && c->Pass == NDR_UNMARSHAL)
                *slot = nullptr;
        }
        break;
    case NDR_FREE:
        present = pointer != nullptr;
        /* an object several full pointers reach is walked and freed once, by the first, which also ends cycles. */
        if (present && type == FC_FP && m->FullPtrXlatTables && !NdrpInBuffer(m, pointer))
            present = NdrFullPointerFree(m->FullPtrXlatTables, pointer) != 0;
        break;
    default:
        return;
    }
    if (!present)
        return;

    if ((flags & NDRF_EMBEDDED) && m->IgnoreEmbeddedPointers)
        return;
    /* long pointer chains are always queued so they do not eat the stack. */
    if ((flags & NDRF_EMBEDDED) && (c->Q->Regions || c->Q->Draining || c->Q->Depth > 64))
        queue_push(c, slot, pointer, pf, refid);
    else
        pointee_walk(c, slot, pointer, pf, refid);
}

void NdrpRegionEnter(NDRC *c)
{
    c->Q->Regions++;
}

void NdrpRegionLeave(NDRC *c)
{
    if (--c->Q->Regions == 0)
        queue_run(c);
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
 * the value a correlation descriptor names: a field next to the array, a field of the enclosing structure, a parameter, a
 * constant, or what a stub routine computes. the descriptor is a type and operator byte and a 16 bit offset. the new kind
 * has more after it, which is not needed to find the value.
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

/* variance descriptors read the same way and give the actual count. */
ULONG_PTR NdrpComputeVariance(PMIDL_STUB_MESSAGE m, uchar *memory, PFORMAT_STRING d)
{
    ULONG_PTR max = m->MaxCount;
    /* a stub routine computing the length may also set the offset, for first_is. */
    m->Offset = 0;
    ULONG_PTR value = NdrpComputeConformance(m, memory, d);
    m->MaxCount = max;
    m->ActualCount = (ULONG)value;
    return value;
}

/* ------------------------------------------------------------------ count checks */

typedef struct NDR_CHECK {
    struct NDR_CHECK *Next;
    PFORMAT_STRING Desc;
    ULONG_PTR Wire;
    ULONG Offset;
    ULONG Actual;
    int Kind;
} NDR_CHECK;

enum { CHECK_COUNT, CHECK_VARIANCE, CHECK_SWITCH };

/* true once everything the descriptor reads is available: a parameter is when it comes before the one being read. */
static BOOL ready(PMIDL_STUB_MESSAGE m, PFORMAT_STRING d)
{
    if ((d[0] & 0xF0) != FC_TOP_LEVEL_CONFORMANCE)
        return TRUE;
    const NDR_PROC *p = (const NDR_PROC *)m->pContext;
    if (!p || d[1] == FC_CALLBACK)
        return !p;
    ULONG offset = *(const USHORT *)(d + 2);
    for (ULONG i = 0; i < p->NumParams; i++) {
        const uchar *pd = p->Params + 6 * i;
        if (*(const USHORT *)(pd + 2) == offset && !(*(const USHORT *)pd & PA_IS_RETURN))
            return (LONG)i < m->ParamNumber;
    }
    return TRUE;
}

static BOOL no_descriptor(PFORMAT_STRING d)
{
    return *(const ULONG *)d == 0xFFFFFFFFu;
}

static BOOL wrong(PMIDL_STUB_MESSAGE m, uchar *memory, int kind, PFORMAT_STRING d, ULONG_PTR wire, ULONG offset, ULONG actual)
{
    ULONG_PTR max = m->MaxCount;
    ULONG o = m->Offset, a = m->ActualCount;
    BOOL result;
    if (kind == CHECK_COUNT) {
        result = (ULONG)NdrpComputeConformance(m, memory, d) != (ULONG)wire;
    } else if (kind == CHECK_VARIANCE) {
        NdrpComputeVariance(m, memory, d);
        result = m->Offset != offset || m->ActualCount != actual;
    } else {
        result = (LONG)NdrpComputeConformance(m, memory, d) != (LONG)wire;
    }
    m->MaxCount = max;
    m->Offset = o;
    m->ActualCount = a;
    return result;
}

static void check(NDRC *c, uchar *memory, int kind, PFORMAT_STRING d, ULONG_PTR wire, ULONG offset, ULONG actual)
{
    PMIDL_STUB_MESSAGE m = c->M;
    if (c->Pass != NDR_UNMARSHAL || no_descriptor(d))
        return;
    uchar fc = d[0] & 0xF0;
    if (fc == FC_TOP_LEVEL_MULTID_CONFORMANCE)
        return;
    if (ready(m, d)) {
        if (wrong(m, memory, kind, d, wire, offset, actual))
            NDR_RAISE(RPC_S_INVALID_BOUND);
        return;
    }
    NDR_CHECK *later = HeapAlloc(GetProcessHeap(), 0, sizeof(*later));
    if (!later)
        NDR_RAISE(RPC_S_OUT_OF_MEMORY);
    later->Desc = d;
    later->Wire = wire;
    later->Offset = offset;
    later->Actual = actual;
    later->Kind = kind;
    later->Next = (NDR_CHECK *)m->pCorrInfo;
    m->pCorrInfo = (PNDR_CORRELATION_INFO)later;
}

void NdrpCheckCount(NDRC *c, uchar *memory, PFORMAT_STRING desc, ULONG_PTR wire)
{
    check(c, memory, CHECK_COUNT, desc, wire, 0, 0);
}

void NdrpCheckVariance(NDRC *c, uchar *memory, PFORMAT_STRING desc, ULONG offset, ULONG actual)
{
    check(c, memory, CHECK_VARIANCE, desc, 0, offset, actual);
}

void NdrpCheckSwitch(NDRC *c, uchar *memory, PFORMAT_STRING desc, LONG_PTR wire)
{
    check(c, memory, CHECK_SWITCH, desc, (ULONG_PTR)wire, 0, 0);
}

/* all parameters are read, check what waited for one. */
void NdrpCheckDeferred(PMIDL_STUB_MESSAGE m)
{
    m->ParamNumber = 0x7FFFFFFF;
    for (NDR_CHECK *later = (NDR_CHECK *)m->pCorrInfo; later; later = later->Next)
        if (wrong(m, nullptr, later->Kind, later->Desc, later->Wire, later->Offset, later->Actual))
            NDR_RAISE(RPC_S_INVALID_BOUND);
}

void NdrpCheckRelease(PMIDL_STUB_MESSAGE m)
{
    NDR_CHECK *later = (NDR_CHECK *)m->pCorrInfo;
    m->pCorrInfo = nullptr;
    while (later) {
        NDR_CHECK *next = later->Next;
        HeapFree(GetProcessHeap(), 0, later);
        later = next;
    }
}

/* ------------------------------------------------------------------ dispatch */

/* for a pointer type, *memory is the pointer itself. */
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
            NdrpAllocObject(c, memory, NdrSimpleMem[fc], flags);
            NdrpSimple(c, *memory, fc);
            break;
        }
        NDR_RAISE(RPC_S_INTERNAL_ERROR);
    }
}

/*
 * creates the queue when there is none, runs it when the walk is done, and removes it even when an exception leaves. a
 * nested walk, as when a stub hands part of an object to an engine function, uses the outer walk's queue.
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
