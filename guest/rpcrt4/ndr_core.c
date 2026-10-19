/*
 * the pieces of the ndr engine every other piece uses: the simple type tables, allocation for the stubs, full pointer
 * tables, the rpc ss allocator and the entry points that start a stub message.
 */
#include "ndr.h"

/* ------------------------------------------------------------------ simple types */

/* indexed by format character; the entries of the characters that are not simple types are zero. */
#define SIMPLE_ROWS(first, second)                                                                                    \
    [FC_BYTE] = first, [FC_CHAR] = first, [FC_SMALL] = first, [FC_USMALL] = first, [FC_WCHAR] = second,                \
    [FC_SHORT] = second, [FC_USHORT] = second

const uchar NdrSimpleAlign[0x100] = {
    SIMPLE_ROWS(0, 1),
    [FC_LONG] = 3,
    [FC_ULONG] = 3,
    [FC_FLOAT] = 3,
    [FC_HYPER] = 7,
    [FC_DOUBLE] = 7,
    [FC_ENUM16] = 1,
    [FC_ENUM32] = 3,
    [FC_IGNORE] = 3,
    [FC_ERROR_STATUS_T] = 3,
    [FC_POINTER] = 3,
    [FC_INT3264] = 3,
    [FC_UINT3264] = 3,
};

const uchar NdrSimpleWire[0x100] = {
    SIMPLE_ROWS(1, 2),
    [FC_LONG] = 4,
    [FC_ULONG] = 4,
    [FC_FLOAT] = 4,
    [FC_HYPER] = 8,
    [FC_DOUBLE] = 8,
    [FC_ENUM16] = 2,
    [FC_ENUM32] = 4,
    [FC_IGNORE] = 4,
    [FC_ERROR_STATUS_T] = 4,
    [FC_POINTER] = 4,
    [FC_INT3264] = 4,
    [FC_UINT3264] = 4,
};

const uchar NdrSimpleMem[0x100] = {
    SIMPLE_ROWS(1, 2),
    [FC_LONG] = 4,
    [FC_ULONG] = 4,
    [FC_FLOAT] = 4,
    [FC_HYPER] = 8,
    [FC_DOUBLE] = 8,
    [FC_ENUM16] = 4,
    [FC_ENUM32] = 4,
    [FC_IGNORE] = sizeof(void *),
    [FC_ERROR_STATUS_T] = 4,
    [FC_POINTER] = sizeof(void *),
    [FC_INT3264] = sizeof(INT_PTR),
    [FC_UINT3264] = sizeof(INT_PTR),
};

/* the three tables are exported under the names midl generated code expects, a byte for each format character. */
RPCRTAPI const unsigned char SimpleTypeAlignment[0xC0] = {
    SIMPLE_ROWS(0, 1),
    [FC_LONG] = 3, [FC_ULONG] = 3, [FC_FLOAT] = 3, [FC_HYPER] = 7, [FC_DOUBLE] = 7, [FC_ENUM16] = 1, [FC_ENUM32] = 3,
    [FC_IGNORE] = 3, [FC_ERROR_STATUS_T] = 3, [FC_INT3264] = 3, [FC_UINT3264] = 3,
};
RPCRTAPI const unsigned char SimpleTypeBufferSize[0xC0] = {
    SIMPLE_ROWS(1, 2),
    [FC_LONG] = 4, [FC_ULONG] = 4, [FC_FLOAT] = 4, [FC_HYPER] = 8, [FC_DOUBLE] = 8, [FC_ENUM16] = 2, [FC_ENUM32] = 4,
    [FC_IGNORE] = 4, [FC_ERROR_STATUS_T] = 4, [FC_INT3264] = 4, [FC_UINT3264] = 4,
};
RPCRTAPI const unsigned char SimpleTypeMemorySize[0xC0] = {
    SIMPLE_ROWS(1, 2),
    [FC_LONG] = 4, [FC_ULONG] = 4, [FC_FLOAT] = 4, [FC_HYPER] = 8, [FC_DOUBLE] = 8, [FC_ENUM16] = 4, [FC_ENUM32] = 4,
    [FC_IGNORE] = sizeof(void *), [FC_ERROR_STATUS_T] = 4, [FC_INT3264] = sizeof(INT_PTR),
    [FC_UINT3264] = sizeof(INT_PTR),
};

RPCRTAPI ULONG RPC_ENTRY NdrGetSimpleTypeBufferAlignment(unsigned char c)
{
    return NdrSimpleAlign[c];
}

RPCRTAPI ULONG RPC_ENTRY NdrGetSimpleTypeBufferSize(unsigned char c)
{
    return NdrSimpleWire[c];
}

RPCRTAPI ULONG RPC_ENTRY NdrGetSimpleTypeMemorySize(unsigned char c)
{
    return NdrSimpleMem[c];
}

/* ------------------------------------------------------------------ allocation */

/*
 * a block that holds a whole tree of pointees: the engine measured the tree and allocated it once, so that the tree
 * is freed by freeing its root.
 */
typedef struct {
    uchar *Base;
    uchar *Next;
    uchar *End;
} ALLOC_ALL;

void *NdrpAlloc(PMIDL_STUB_MESSAGE m, SIZE_T size)
{
    ALLOC_ALL *all = (ALLOC_ALL *)m->pAllocAllNodesContext;
    if (all) {
        uchar *p = (uchar *)(((ULONG_PTR)all->Next + 7) & ~(ULONG_PTR)7);
        if (p >= all->Next && p <= all->End && size <= (SIZE_T)(all->End - p)) {
            all->Next = p + size;
            memset(p, 0, size);
            return p;
        }
    }
    void *p = m->pfnAllocate ? m->pfnAllocate(size) : nullptr;
    if (!p)
        NDR_RAISE(RPC_S_OUT_OF_MEMORY);
    return p;
}

void NdrpFree(PMIDL_STUB_MESSAGE m, void *memory)
{
    if (memory && m->pfnFree)
        m->pfnFree(memory);
}

/* a pointer into the receive buffer is not the engine's to free. */
BOOL NdrpInBuffer(PMIDL_STUB_MESSAGE m, const void *memory)
{
    return m->BufferStart && (const uchar *)memory >= m->BufferStart && (const uchar *)memory <= m->BufferEnd;
}

RPCRTAPI void *RPC_ENTRY NdrAllocate(PMIDL_STUB_MESSAGE m, size_t length)
{
    void *p = m->pfnAllocate(length);
    if (!p)
        NDR_RAISE(RPC_S_OUT_OF_MEMORY);
    return p;
}

/* ------------------------------------------------------------------ full pointers */

/*
 * the tables keep two hashes: a pointer to its reference id and to the state of what was done with it, and a
 * reference id to the pointer the wire named by it. the public fields hold the hashes.
 */
#define FP_PTR_STATE_FREED 0x20

typedef struct FP_REFID {
    struct FP_REFID *Next;
    void *Pointer;
    ULONG RefId;
    unsigned char State;
    PFORMAT_STRING Format;
} FP_REFID;

static ULONG fp_hash_pointer(PFULL_PTR_XLAT_TABLES t, const void *pointer)
{
    return (ULONG)(((ULONG_PTR)pointer >> 4) & t->PointerToRefId.HashMask);
}

static void *fp_alloc(SIZE_T size)
{
    void *p = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, size);
    if (!p)
        NDR_RAISE(RPC_S_OUT_OF_MEMORY);
    return p;
}

RPCRTAPI void RPC_ENTRY NdrFullPointerXlatFree(PFULL_PTR_XLAT_TABLES tables)
{
    if (!tables)
        return;
    if (tables->PointerToRefId.XlatTable) {
        for (ULONG i = 0; i < tables->PointerToRefId.NumberOfBuckets; i++) {
            PFULL_PTR_TO_REFID_ELEMENT e = tables->PointerToRefId.XlatTable[i];
            while (e) {
                PFULL_PTR_TO_REFID_ELEMENT next = e->Next;
                HeapFree(GetProcessHeap(), 0, e);
                e = next;
            }
        }
        HeapFree(GetProcessHeap(), 0, tables->PointerToRefId.XlatTable);
    }
    FP_REFID **buckets = (FP_REFID **)tables->RefIdToPointer.XlatTable;
    if (buckets) {
        for (ULONG i = 0; i < tables->RefIdToPointer.NumberOfEntries; i++) {
            FP_REFID *e = buckets[i];
            while (e) {
                FP_REFID *next = e->Next;
                HeapFree(GetProcessHeap(), 0, e);
                e = next;
            }
        }
        HeapFree(GetProcessHeap(), 0, buckets);
    }
    HeapFree(GetProcessHeap(), 0, tables);
}

RPCRTAPI PFULL_PTR_XLAT_TABLES RPC_ENTRY NdrFullPointerXlatInit(ULONG pointers, XLAT_SIDE side)
{
    /* the number of pointers sizes the tables, rounded up to a power of two; none means 128. */
    ULONG buckets = 128;
    if (pointers) {
        buckets = 1;
        while (buckets < pointers && buckets < 0x80000000u)
            buckets <<= 1;
    }
    PFULL_PTR_XLAT_TABLES t = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*t));
    if (t) {
        t->PointerToRefId.XlatTable = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, buckets * sizeof(void *));
        t->RefIdToPointer.XlatTable = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, buckets * sizeof(void *));
    }
    if (!t || !t->PointerToRefId.XlatTable || !t->RefIdToPointer.XlatTable) {
        NdrFullPointerXlatFree(t);
        if (side == XLAT_SERVER)
            NDR_RAISE(RPC_S_OUT_OF_MEMORY);
        return nullptr;
    }
    t->PointerToRefId.NumberOfBuckets = buckets;
    t->PointerToRefId.HashMask = buckets - 1;
    t->RefIdToPointer.NumberOfEntries = buckets;
    t->NextRefId = 1;
    t->XlatSide = side;
    return t;
}

static PFULL_PTR_TO_REFID_ELEMENT fp_find_pointer(PFULL_PTR_XLAT_TABLES t, void *pointer)
{
    for (PFULL_PTR_TO_REFID_ELEMENT e = t->PointerToRefId.XlatTable[fp_hash_pointer(t, pointer)]; e; e = e->Next)
        if (e->Pointer == pointer)
            return e;
    return nullptr;
}

static FP_REFID *fp_find_refid(PFULL_PTR_XLAT_TABLES t, ULONG refid, BOOL create)
{
    FP_REFID **buckets = (FP_REFID **)t->RefIdToPointer.XlatTable;
    ULONG slot = refid & (t->RefIdToPointer.NumberOfEntries - 1);
    for (FP_REFID *e = buckets[slot]; e; e = e->Next)
        if (e->RefId == refid)
            return e;
    if (!create)
        return nullptr;
    FP_REFID *e = fp_alloc(sizeof(*e));
    e->RefId = refid;
    e->Next = buckets[slot];
    buckets[slot] = e;
    return e;
}

static ULONG fp_next_id(PFULL_PTR_XLAT_TABLES t)
{
    /* ids are 31 bits; running out means the wire is not what it says. */
    ULONG id = t->NextRefId & 0x7FFFFFFF;
    t->NextRefId++;
    if (!id)
        NDR_RAISE(RPC_X_BAD_STUB_DATA);
    return id;
}

/* 1 when the pointer is null or what the query asks about was done already, with its id. */
RPCRTAPI int RPC_ENTRY NdrFullPointerQueryPointer(PFULL_PTR_XLAT_TABLES tables, void *pointer, unsigned char query,
                                                  ULONG *ref_id)
{
    if (!pointer) {
        if (ref_id)
            *ref_id = 0;
        return 1;
    }
    if (!tables)
        NDR_RAISE(RPC_S_OUT_OF_MEMORY);
    PFULL_PTR_TO_REFID_ELEMENT e = fp_find_pointer(tables, pointer);
    if (e) {
        if (query & e->State) {
            if (ref_id)
                *ref_id = e->RefId;
            return 1;
        }
        if (ref_id && !e->RefId)
            e->RefId = fp_next_id(tables);
        e->State |= query;
    } else {
        e = fp_alloc(sizeof(*e));
        e->State |= query;
        e->Pointer = pointer;
        if (ref_id)
            e->RefId = fp_next_id(tables);
        ULONG slot = fp_hash_pointer(tables, pointer);
        e->Next = tables->PointerToRefId.XlatTable[slot];
        tables->PointerToRefId.XlatTable[slot] = e;
    }
    if (!ref_id)
        return 0;
    *ref_id = e->RefId;
    if (tables->XlatSide == XLAT_CLIENT) {
        /* a server that answers with this id gets the pointer back. */
        FP_REFID *r = fp_find_refid(tables, e->RefId, TRUE);
        r->Pointer = pointer;
    }
    return 0;
}

/* 1 when the id is null or what the query asks about was done already; the pointer it was given comes back. */
RPCRTAPI int RPC_ENTRY NdrFullPointerQueryRefId(PFULL_PTR_XLAT_TABLES tables, ULONG ref_id, unsigned char query,
                                                void **pointer)
{
    if (!ref_id)
        return 1;
    if (!tables)
        NDR_RAISE(RPC_S_OUT_OF_MEMORY);
    if (ref_id >= tables->NextRefId) {
        ULONG next = (ref_id + 1) & 0x7FFFFFFF;
        tables->NextRefId = next;
        if (next < ref_id)
            NDR_RAISE(RPC_X_BAD_STUB_DATA);
    }
    FP_REFID *r = fp_find_refid(tables, ref_id, TRUE);
    if (pointer)
        *pointer = r->Pointer;
    if (!(r->State & query)) {
        r->State |= query;
        return 0;
    }
    return 1;
}

RPCRTAPI void RPC_ENTRY NdrFullPointerInsertRefId(PFULL_PTR_XLAT_TABLES tables, ULONG ref_id, void *pointer)
{
    if (!tables)
        NDR_RAISE(RPC_S_OUT_OF_MEMORY);
    FP_REFID *r = fp_find_refid(tables, ref_id, FALSE);
    if (!r)
        NDR_RAISE(RPC_S_INTERNAL_ERROR);
    r->Pointer = pointer;
    if (tables->XlatSide == XLAT_SERVER && pointer) {
        PFULL_PTR_TO_REFID_ELEMENT e = fp_alloc(sizeof(*e));
        e->Pointer = pointer;
        e->RefId = ref_id;
        ULONG slot = fp_hash_pointer(tables, pointer);
        e->Next = tables->PointerToRefId.XlatTable[slot];
        tables->PointerToRefId.XlatTable[slot] = e;
    }
}

/* 1 when the memory is to be freed now, 0 when another pointer to it was freed already. */
RPCRTAPI int RPC_ENTRY NdrFullPointerFree(PFULL_PTR_XLAT_TABLES tables, void *pointer)
{
    if (!pointer)
        return 0;
    if (!tables)
        NDR_RAISE(RPC_S_OUT_OF_MEMORY);
    PFULL_PTR_TO_REFID_ELEMENT e = fp_find_pointer(tables, pointer);
    if (!e) {
        e = fp_alloc(sizeof(*e));
        e->Pointer = pointer;
        e->State = FP_PTR_STATE_FREED;
        ULONG slot = fp_hash_pointer(tables, pointer);
        e->Next = tables->PointerToRefId.XlatTable[slot];
        tables->PointerToRefId.XlatTable[slot] = e;
        return 1;
    }
    if (!(e->State & FP_PTR_STATE_FREED)) {
        e->State |= FP_PTR_STATE_FREED;
        return 1;
    }
    return 0;
}

/* ------------------------------------------------------------------ the rpc ss allocator */

/* memory handed out between an enable and a disable, freed together. */
typedef struct SS_NODE {
    struct SS_NODE *Next;
    struct SS_NODE *Prev;
} SS_NODE;

typedef struct {
    SS_NODE Head;
    LONG Enabled;
    RPC_CLIENT_ALLOC *Alloc;
    RPC_CLIENT_FREE *Free;
} SS_STATE;

static DWORD g_ss_tls = TLS_OUT_OF_INDEXES;

static SS_STATE *ss_state(BOOL create)
{
    if (g_ss_tls == TLS_OUT_OF_INDEXES) {
        DWORD slot = TlsAlloc();
        if (InterlockedCompareExchange((volatile LONG *)&g_ss_tls, (LONG)slot, (LONG)TLS_OUT_OF_INDEXES) !=
            (LONG)TLS_OUT_OF_INDEXES)
            TlsFree(slot);
    }
    SS_STATE *s = TlsGetValue(g_ss_tls);
    if (!s && create) {
        s = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*s));
        if (s) {
            s->Head.Next = s->Head.Prev = &s->Head;
            TlsSetValue(g_ss_tls, s);
        }
    }
    return s;
}

RPCRTAPI void RPC_ENTRY RpcSsEnableAllocate(void)
{
    SS_STATE *s = ss_state(TRUE);
    if (!s)
        NDR_RAISE(RPC_S_OUT_OF_MEMORY);
    s->Enabled++;
}

static void ss_release_all(SS_STATE *s)
{
    SS_NODE *n = s->Head.Next;
    while (n != &s->Head) {
        SS_NODE *next = n->Next;
        HeapFree(GetProcessHeap(), 0, n);
        n = next;
    }
    s->Head.Next = s->Head.Prev = &s->Head;
}

RPCRTAPI void RPC_ENTRY RpcSsDisableAllocate(void)
{
    SS_STATE *s = ss_state(FALSE);
    if (!s || s->Enabled <= 0)
        return;
    if (--s->Enabled == 0)
        ss_release_all(s);
}

RPCRTAPI void *RPC_ENTRY RpcSsAllocate(size_t size)
{
    SS_STATE *s = ss_state(TRUE);
    if (!s)
        NDR_RAISE(RPC_S_OUT_OF_MEMORY);
    SS_NODE *n = HeapAlloc(GetProcessHeap(), 0, sizeof(SS_NODE) + size + 16);
    if (!n)
        NDR_RAISE(RPC_S_OUT_OF_MEMORY);
    n->Next = s->Head.Next;
    n->Prev = &s->Head;
    s->Head.Next->Prev = n;
    s->Head.Next = n;
    /* the user part starts at a 16 byte boundary after the header. */
    return (uchar *)n + 16;
}

RPCRTAPI void RPC_ENTRY RpcSsFree(void *node)
{
    if (!node)
        return;
    SS_NODE *n = (SS_NODE *)((uchar *)node - 16);
    n->Prev->Next = n->Next;
    n->Next->Prev = n->Prev;
    HeapFree(GetProcessHeap(), 0, n);
}

RPCRTAPI RPC_SS_THREAD_HANDLE RPC_ENTRY RpcSsGetThreadHandle(void)
{
    return ss_state(FALSE);
}

RPCRTAPI void RPC_ENTRY RpcSsSetThreadHandle(RPC_SS_THREAD_HANDLE id)
{
    ss_state(TRUE);
    TlsSetValue(g_ss_tls, id);
}

RPCRTAPI void RPC_ENTRY RpcSsSetClientAllocFree(RPC_CLIENT_ALLOC *alloc, RPC_CLIENT_FREE *free_function)
{
    SS_STATE *s = ss_state(TRUE);
    if (s) {
        s->Alloc = alloc;
        s->Free = free_function;
    }
}

RPCRTAPI void RPC_ENTRY RpcSsSwapClientAllocFree(RPC_CLIENT_ALLOC *alloc, RPC_CLIENT_FREE *free_function,
                                                 RPC_CLIENT_ALLOC **old_alloc, RPC_CLIENT_FREE **old_free)
{
    SS_STATE *s = ss_state(TRUE);
    if (!s)
        return;
    *old_alloc = s->Alloc;
    *old_free = s->Free;
    s->Alloc = alloc;
    s->Free = free_function;
}

RPCRTAPI void *RPC_ENTRY RpcSmAllocate(size_t size, RPC_STATUS *status)
{
    SS_STATE *s = ss_state(TRUE);
    *status = s ? RPC_S_OK : RPC_S_OUT_OF_MEMORY;
    return s ? RpcSsAllocate(size) : nullptr;
}

RPCRTAPI RPC_STATUS RPC_ENTRY RpcSmClientFree(void *node)
{
    SS_STATE *s = ss_state(FALSE);
    if (s && s->Free)
        s->Free(node);
    else
        HeapFree(GetProcessHeap(), 0, node);
    return RPC_S_OK;
}

RPCRTAPI RPC_STATUS RPC_ENTRY RpcSmDisableAllocate(void)
{
    RpcSsDisableAllocate();
    return RPC_S_OK;
}

RPCRTAPI RPC_STATUS RPC_ENTRY RpcSmEnableAllocate(void)
{
    SS_STATE *s = ss_state(TRUE);
    if (!s)
        return RPC_S_OUT_OF_MEMORY;
    s->Enabled++;
    return RPC_S_OK;
}

RPCRTAPI RPC_STATUS RPC_ENTRY RpcSmFree(void *node)
{
    RpcSsFree(node);
    return RPC_S_OK;
}

RPCRTAPI RPC_SS_THREAD_HANDLE RPC_ENTRY RpcSmGetThreadHandle(RPC_STATUS *status)
{
    *status = RPC_S_OK;
    return RpcSsGetThreadHandle();
}

RPCRTAPI RPC_STATUS RPC_ENTRY RpcSmSetThreadHandle(RPC_SS_THREAD_HANDLE id)
{
    RpcSsSetThreadHandle(id);
    return RPC_S_OK;
}

RPCRTAPI RPC_STATUS RPC_ENTRY RpcSmSetClientAllocFree(RPC_CLIENT_ALLOC *alloc, RPC_CLIENT_FREE *free_function)
{
    RpcSsSetClientAllocFree(alloc, free_function);
    return RPC_S_OK;
}

RPCRTAPI RPC_STATUS RPC_ENTRY RpcSmSwapClientAllocFree(RPC_CLIENT_ALLOC *alloc, RPC_CLIENT_FREE *free_function,
                                                       RPC_CLIENT_ALLOC **old_alloc, RPC_CLIENT_FREE **old_free)
{
    RpcSsSwapClientAllocFree(alloc, free_function, old_alloc, old_free);
    return RPC_S_OK;
}

RPCRTAPI RPC_STATUS RPC_ENTRY RpcSmDestroyClientContext(void **context)
{
    RpcSsDestroyClientContext(context);
    return RPC_S_OK;
}

RPCRTAPI void RPC_ENTRY RpcUserFree(handle_t handle, void *buffer)
{
    RpcSsFree(buffer);
}

RPCRTAPI void *RPC_ENTRY NdrRpcSsDefaultAllocate(size_t size)
{
    return RpcSsAllocate(size);
}

RPCRTAPI void RPC_ENTRY NdrRpcSsDefaultFree(void *node)
{
    RpcSsFree(node);
}

RPCRTAPI void *RPC_ENTRY NdrRpcSmClientAllocate(size_t size)
{
    SS_STATE *s = ss_state(FALSE);
    return s && s->Alloc ? s->Alloc(size) : HeapAlloc(GetProcessHeap(), 0, size);
}

RPCRTAPI void RPC_ENTRY NdrRpcSmClientFree(void *node)
{
    RpcSmClientFree(node);
}

RPCRTAPI void RPC_ENTRY NdrRpcSsEnableAllocate(PMIDL_STUB_MESSAGE m)
{
    RpcSsEnableAllocate();
    m->pfnAllocate = NdrRpcSsDefaultAllocate;
    m->pfnFree = NdrRpcSsDefaultFree;
}

RPCRTAPI void RPC_ENTRY NdrRpcSsDisableAllocate(PMIDL_STUB_MESSAGE m)
{
    RpcSsDisableAllocate();
    m->pfnAllocate = m->StubDesc->pfnAllocate;
    m->pfnFree = m->StubDesc->pfnFree;
}

RPCRTAPI void RPC_ENTRY NdrRpcSmSetClientToOsf(PMIDL_STUB_MESSAGE m)
{
    m->pfnAllocate = NdrRpcSmClientAllocate;
    m->pfnFree = NdrRpcSmClientFree;
}

/* ------------------------------------------------------------------ memory for the ole extension */

RPCRTAPI void *RPC_ENTRY I_RpcAllocate(unsigned int size)
{
    return HeapAlloc(GetProcessHeap(), 0, size);
}

RPCRTAPI void RPC_ENTRY I_RpcFree(void *object)
{
    HeapFree(GetProcessHeap(), 0, object);
}

RPCRTAPI void RPC_ENTRY I_RpcPauseExecution(ULONG milliseconds)
{
    Sleep(milliseconds);
}

#ifdef NDR_TRACE
void NdrpTraceRaise(ULONG status, const char *file, int line)
{
    char text[200];
    char *out = text;
    for (const char *t = "rpc raise "; *t; t++)
        *out++ = *t;
    for (const char *f = file; *f; f++)
        *out++ = *f;
    *out++ = ':';
    char digits[12];
    int count = 0;
    for (int v = line; v || !count; v /= 10)
        digits[count++] = (char)('0' + v % 10);
    while (count)
        *out++ = digits[--count];
    *out++ = ' ';
    for (int shift = 28; shift >= 0; shift -= 4)
        *out++ = "0123456789abcdef"[(status >> shift) & 15];
    *out++ = '\n';
    *out = 0;
    OutputDebugStringA(text);
}
#endif
