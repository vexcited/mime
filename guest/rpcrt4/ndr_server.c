/*
 * the server side of the interpreter: unmarshal a request, call the manager routine, marshal the results into the reply.
 * NdrServerCall2 serves rpc runtime calls and NdrStubCall2 com stub calls, which also come with their buffer's channel.
 */
#include "ndr_proc.h"
#include <mime/win32/rpcproxy.h>

static BOOL is_pointer_type(uchar fc)
{
    return (fc >= FC_RP && fc <= FC_FP) || fc == FC_IP || fc == FC_BIND_CONTEXT;
}

/* cells a call needs, taken from the frame's own space first. */
typedef struct {
    uchar Local[512];
    ULONG Used;
    void *Chunks[16];
    ULONG ChunkCount;
} ARENA;

static void *arena_alloc(ARENA *a, SIZE_T size)
{
    size = (size + 15) & ~(SIZE_T)15;
    if (a->Used + size <= sizeof(a->Local)) {
        void *p = a->Local + a->Used;
        a->Used += (ULONG)size;
        memset(p, 0, size);
        return p;
    }
    if (a->ChunkCount == ARRAYSIZE(a->Chunks))
        NDR_RAISE(RPC_S_OUT_OF_MEMORY);
    void *p = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, size);
    if (!p)
        NDR_RAISE(RPC_S_OUT_OF_MEMORY);
    a->Chunks[a->ChunkCount++] = p;
    return p;
}

static void arena_free(ARENA *a)
{
    for (ULONG i = 0; i < a->ChunkCount; i++)
        HeapFree(GetProcessHeap(), 0, a->Chunks[i]);
}

typedef struct {
    NDR_PROC *P;
    ARENA *Arena;
} SERVER;

static NDR_PARAM param_of(const NDR_PROC *p, ULONG i)
{
    const uchar *d = p->Params + 6 * i;
    NDR_PARAM prm = {*(const USHORT *)d, *(const USHORT *)(d + 2), *(const USHORT *)(d + 4), d[4]};
    return prm;
}

static void param_walk(PMIDL_STUB_MESSAGE m, NDR_PASS pass, const NDR_PROC *p, const NDR_PARAM *prm, uchar *slot)
{
    PFORMAT_STRING type = p->Types + prm->TypeOffset;
    m->fIsIn = (prm->Attrs & PA_IS_IN) != 0;
    m->fIsOut = (prm->Attrs & PA_IS_OUT) != 0;
    if (is_pointer_type(type[0])) {
        NdrpWalkTop(m, pass, (uchar **)slot, type, 0);
        return;
    }
    uchar *object = (prm->Attrs & PA_IS_BY_VALUE) ? slot : *(uchar **)slot;
    NdrpWalkTop(m, pass, &object, type, 0);
    if (pass == NDR_UNMARSHAL && !(prm->Attrs & PA_IS_BY_VALUE))
        *(uchar **)slot = object;
}

static void simple_value(PMIDL_STUB_MESSAGE m, NDR_PASS pass, uchar *slot, uchar fc)
{
    NDRC c = {m, pass, nullptr};
    NdrpSimple(&c, slot, fc);
}


static void unmarshal_in(PMIDL_STUB_MESSAGE m, const NDR_PROC *p, ARENA *arena)
{
    for (ULONG i = 0; i < p->NumParams; i++) {
        NDR_PARAM prm = param_of(p, i);
        m->ParamNumber = (LONG)i;
        if ((prm.Attrs & (PA_IS_IN | PA_IS_PIPE)) != PA_IS_IN)
            continue;
        uchar *slot = p->Args + prm.Offset;
        if (prm.Attrs & 0x800) {
            NDRC c = {m, NDR_UNMARSHAL, nullptr};
            NdrpAlign(&c, 3);
            *(ULONG_PTR *)slot = *(ULONG *)NdrpTake(&c, 4) != 0;
            continue;
        }
        if (prm.Attrs & PA_IS_BASETYPE) {
            if (prm.Attrs & PA_IS_SIMPLE_REF) {
                /* a ref pointer to a simple type gets a cell holding the value. */
                uchar *cell = arena_alloc(arena, 16);
                *(uchar **)slot = cell;
                simple_value(m, NDR_UNMARSHAL, cell, prm.BaseType);
            } else {
                simple_value(m, NDR_UNMARSHAL, slot, prm.BaseType);
            }
            continue;
        }
        if (prm.Attrs >= 0x2000)
            *(uchar **)slot = arena_alloc(arena, 8 * (prm.Attrs >> PA_SERVER_ALLOC_SHIFT));
        else if (p->Types[prm.TypeOffset] == FC_BIND_CONTEXT && (p->Types[prm.TypeOffset + 1] & HANDLE_PARAM_IS_VIA_PTR) &&
                 !*(uchar **)slot)
            *(uchar **)slot = arena_alloc(arena, sizeof(void *));
        param_walk(m, NDR_UNMARSHAL, p, &prm, slot);
    }
    /* counts that depended on a later parameter. */
    NdrpCheckDeferred(m);
}

/*
 * zeroed memory for an [out] reference pointer, which the server routine fills in. a pointer to a pointer gets a cell for
 * the inner pointer, and so on down the chain.
 */
static void out_init(PMIDL_STUB_MESSAGE m, PFORMAT_STRING type, uchar **slot, ARENA *arena)
{
    while (type[0] == FC_RP) {
        if (type[1] & FC_SIMPLE_POINTER) {
            *slot = arena_alloc(arena, NdrSimpleMem[type[2]]);
            return;
        }
        if (type[1] & FC_POINTER_DEREF) {
            uchar *cell = arena_alloc(arena, sizeof(void *));
            *slot = cell;
            PFORMAT_STRING inner = type + 2 + *(const short *)(type + 2);
            if (inner[0] == FC_RP && (inner[1] & FC_POINTER_DEREF)) {
                slot = (uchar **)cell;
                type = inner;
                continue;
            }
            return;
        }
        type = type + 2 + *(const short *)(type + 2);
        break;
    }
    if (type[0] == FC_BLKHOLE)
        type += 2 + *(const short *)(type + 2);
    if (type[0] == FC_BIND_CONTEXT) {
        *slot = (type[1] & HANDLE_PARAM_IS_VIA_PTR) ? arena_alloc(arena, sizeof(void *)) : nullptr;
        return;
    }
    SIZE_T size = NdrpOutMemory(m, type);
    if (size > NDR_MAX_ALLOC)
        NDR_RAISE(RPC_S_OUT_OF_MEMORY);
    *slot = arena_alloc(arena, size ? size : 8);
}

static void init_out(PMIDL_STUB_MESSAGE m, const NDR_PROC *p, ARENA *arena)
{
    for (ULONG i = 0; i < p->NumParams; i++) {
        NDR_PARAM prm = param_of(p, i);
        if (prm.Attrs & 0x800)
            continue;
        if ((prm.Attrs & PA_IS_IN) || (prm.Attrs & PA_IS_RETURN) || (prm.Attrs & PA_IS_PIPE))
            continue;
        if (!(prm.Attrs & PA_IS_OUT))
            continue;
        uchar **slot = (uchar **)(p->Args + prm.Offset);
        if (prm.Attrs >= 0x2000) {
            *slot = arena_alloc(arena, 8 * (prm.Attrs >> PA_SERVER_ALLOC_SHIFT));
            continue;
        }
        if (prm.Attrs & PA_IS_BASETYPE) {
            *slot = arena_alloc(arena, 16);
            continue;
        }
        out_init(m, p->Types + prm.TypeOffset, slot, arena);
    }
}

static void size_out(PMIDL_STUB_MESSAGE m, const NDR_PROC *p)
{
    m->BufferLength = p->ServerSize;
    if (!(p->Oi2Flags & OIF_SERVER_MUST_SIZE))
        return;
    for (ULONG i = 0; i < p->NumParams; i++) {
        NDR_PARAM prm = param_of(p, i);
        if ((prm.Attrs & (PA_IS_OUT | PA_MUST_SIZE)) != (PA_IS_OUT | PA_MUST_SIZE))
            continue;
        uchar *slot = p->Args + prm.Offset;
        if (prm.Attrs & PA_IS_BASETYPE) {
            if (prm.Attrs & PA_IS_SIMPLE_REF)
                slot = *(uchar **)slot;
            simple_value(m, NDR_SIZE, slot, prm.BaseType);
            continue;
        }
        param_walk(m, NDR_SIZE, p, &prm, slot);
    }
}

static void marshal_out(PMIDL_STUB_MESSAGE m, const NDR_PROC *p)
{
    for (ULONG i = 0; i < p->NumParams; i++) {
        NDR_PARAM prm = param_of(p, i);
        if ((prm.Attrs & (PA_IS_OUT | PA_IS_PIPE)) != PA_IS_OUT)
            continue;
        uchar *slot = p->Args + prm.Offset;
        if (prm.Attrs & PA_IS_BASETYPE) {
            if (prm.Attrs & PA_IS_SIMPLE_REF)
                slot = *(uchar **)slot;
            if (prm.BaseType == FC_ENUM16 && (*(ULONG *)slot & 0xFFFF8000u))
                NDR_RAISE(RPC_X_ENUM_VALUE_OUT_OF_RANGE);
            simple_value(m, NDR_MARSHAL, slot, prm.BaseType);
            continue;
        }
        param_walk(m, NDR_MARSHAL, p, &prm, slot);
    }
}

/* frees the [in] data allocated for the call, as the parameter attributes ask. */
static void free_params(PMIDL_STUB_MESSAGE m, const NDR_PROC *p)
{
    for (ULONG i = 0; i < p->NumParams; i++) {
        NDR_PARAM prm = param_of(p, i);
        if (!(prm.Attrs & PA_MUST_FREE))
            continue;
        if (prm.Attrs & PA_IS_BASETYPE)
            continue;
        uchar *slot = p->Args + prm.Offset;
        PFORMAT_STRING type = p->Types + prm.TypeOffset;
        uchar *object = (prm.Attrs & PA_IS_BY_VALUE) ? slot : *(uchar **)slot;
        if (!is_pointer_type(type[0]) && !object)
            continue;
        m->fInFree = 1;
        if (is_pointer_type(type[0])) {
            NdrpWalkTop(m, NDR_FREE, (uchar **)slot, type, 0);
        } else {
            uchar *walk = object;
            NdrpWalkTop(m, NDR_FREE, &walk, type, 0);
            /* the stub only allocated a simple reference pointee for an [in] parameter, so only then does it free it. */
            if ((prm.Attrs & PA_IS_SIMPLE_REF) && (prm.Attrs & PA_IS_IN) && !(prm.Attrs & PA_IS_BY_VALUE) && prm.Attrs < 0x2000 && object &&
                !NdrpInBuffer(m, object))
                NdrpFree(m, object);
        }
        m->fInFree = 0;
    }
}


static BOOL arena_owns(const ARENA *a, const void *p)
{
    return (const uchar *)p >= a->Local && (const uchar *)p < a->Local + sizeof(a->Local);
}

LONG RPC_ENTRY NdrStubCall2(IRpcStubBuffer *stub, IRpcChannelBuffer *channel, PRPC_MESSAGE msg, ULONG *phase)
{
    const MIDL_SERVER_INFO *info;
    void *object = nullptr;
    void **epv;
    ULONG unused_phase;
    if (!phase)
        phase = &unused_phase;
    if (stub) {
        CStdStubBuffer *s = (CStdStubBuffer *)stub;
        const CInterfaceStubHeader *header = (const CInterfaceStubHeader *)((const uchar *)s->lpVtbl - sizeof(*header));
        info = header->pServerInfo;
        object = s->pvServerObject;
        epv = object ? *(void ***)object : nullptr;
    } else {
        const RPC_SERVER_INTERFACE *iface = msg->RpcInterfaceInformation;
        info = iface->InterpreterInfo;
        epv = msg->ManagerEpv ? msg->ManagerEpv : (void **)info->DispatchTable;
    }
    /* the runtime or the stub vtable already checked the procedure number against the dispatch table. */
    PMIDL_STUB_DESC desc = info->pStubDesc;
    PFORMAT_STRING procs = info->ProcString;
    const unsigned short *offsets = info->FmtStringOffset;
    PFORMAT_STRING types = desc->pFormatTypes;
    MIDL_STUB_DESC old_desc;
    /* a dual syntax stub keeps the ndr formats in its syntax table. */
    if (info->pSyntaxInfo) {
        const MIDL_SYNTAX_INFO *old = NdrpOldSyntax(info->pSyntaxInfo, info->nCount ? info->nCount : 2);
        if (!old)
            NDR_RAISE(RPC_S_UNSUPPORTED_TRANS_SYN);
        procs = old->ProcString;
        offsets = old->FmtStringOffset;
        types = old->TypeString;
        old_desc = *desc;
        old_desc.pFormatTypes = types;
        if (old->aUserMarshalQuadruple)
            old_desc.aUserMarshalQuadruple = (const USER_MARSHAL_ROUTINE_QUADRUPLE *)old->aUserMarshalQuadruple;
        desc = &old_desc;
    }
    PFORMAT_STRING format = procs + offsets[msg->ProcNum];

    NDR_PROC p;
    NdrpParseProc(format, &p);
    p.Types = types;

    MIDL_STUB_MESSAGE m;
    NdrpInitStubMessage(&m, msg, desc, FALSE, &p);
    m.Buffer = m.BufferStart = msg->Buffer;
    m.BufferEnd = m.BufferStart + msg->BufferLength;
    m.fBufferValid = 1;
    m.pRpcChannelBuffer = channel;
    m.pContext = (struct _NDR_PROC_CONTEXT *)&p;
    *phase = 0;

    ULONG frame_bytes = (p.StackSize + 96) & ~15u;
    ULONG_PTR local_frame[48];
    uchar *frame = frame_bytes <= sizeof(local_frame) ? (uchar *)local_frame : nullptr;
    ARENA *arena = HeapAlloc(GetProcessHeap(), 0, sizeof(*arena));
    if (!arena)
        NDR_RAISE(RPC_S_OUT_OF_MEMORY);
    arena->Used = 0;
    arena->ChunkCount = 0;
    if (!frame) {
        frame = HeapAlloc(GetProcessHeap(), 0, frame_bytes);
        if (!frame) {
            HeapFree(GetProcessHeap(), 0, arena);
            NDR_RAISE(RPC_S_OUT_OF_MEMORY);
        }
    }
    memset(frame, 0, frame_bytes);
    p.Args = frame;
    m.StackTop = frame;

    BOOL is_object = (p.OiFlags & Oi_OBJECT_PROC) != 0;
    ULONG slot_size = sizeof(void *);
    LONG result = 0;
    ULONG fault = 0;
    BOOL faulted = FALSE;

    __try {
        if (p.OiFlags & Oi_FULL_PTR_USED)
            m.FullPtrXlatTables = NdrFullPointerXlatInit(0, XLAT_SERVER);
        if (p.OiFlags & Oi_RPCSS_ALLOC_USED)
            NdrRpcSsEnableAllocate(&m);
        if (is_object)
            *(void **)frame = object;
        /* an explicit handle parameter gets the call's handle. */
        if (p.HandleType == 0 && p.HandleDesc[0] == FC_BIND_PRIMITIVE) {
            uchar *h = frame + *(const USHORT *)(p.HandleDesc + 2);
            if (p.HandleDesc[1] & HANDLE_PARAM_IS_VIA_PTR)
                h = *(uchar **)h;
            if (h)
                *(void **)h = msg->Handle;
        }
        if ((msg->DataRepresentation & NDR_INT_REP_MASK) != NDR_LITTLE_ENDIAN)
            NDR_RAISE(RPC_S_UNSUPPORTED_TRANS_SYN);
        unmarshal_in(&m, &p, arena);
        if ((ULONG)(m.Buffer - m.BufferStart) > msg->BufferLength)
            NDR_RAISE(RPC_X_BAD_STUB_DATA);
        init_out(&m, &p, arena);
        *phase = 1;
        const STUB_THUNK *thunks = info->ThunkTable;
        if (thunks && thunks[msg->ProcNum]) {
            thunks[msg->ProcNum](&m);
        } else {
            ULONG nargs = p.StackSize / slot_size;
            ULONG ret_offset = 0, ret_kind = 0;
            if ((p.Oi2Flags & OIF_HAS_RETURN) && p.NumParams) {
                /* the return value's slot follows the arguments, and a float result is not in the integer register. */
                NDR_PARAM rp = param_of(&p, p.NumParams - 1);
                ret_offset = rp.Offset;
                nargs = ret_offset / slot_size;
                if (rp.Attrs & PA_IS_BASETYPE)
                    ret_kind = rp.BaseType;
            }
            ULONGLONG fp = 0;
            ULONGLONG value = NdrpInvokeEx(epv[msg->ProcNum], (const ULONG_PTR *)frame, nargs,
                                           ret_kind == FC_FLOAT ? 1 : ret_kind == FC_DOUBLE ? 2 : 0, &fp);
            if (p.Oi2Flags & OIF_HAS_RETURN) {
                if (ret_kind == FC_FLOAT || ret_kind == FC_DOUBLE)
                    value = fp;
                memcpy(frame + ret_offset, &value, ret_kind == FC_HYPER || ret_kind == FC_DOUBLE ? 8 : sizeof(ULONG_PTR));
            }
        }
        *phase = 2;
        size_out(&m, &p);
        m.BufferLength += NDR_BUFFER_SLACK;
        if (channel) {
            msg->BufferLength = m.BufferLength;
            NdrStubGetBuffer(stub, channel, &m);
        } else {
            msg->BufferLength = (m.BufferLength + 3) & ~3u;
            RPC_STATUS status = I_RpcGetBufferWithObject(msg, nullptr);
            if (status)
                NDR_RAISE(status);
            m.Buffer = m.BufferStart = msg->Buffer;
            m.BufferEnd = m.BufferStart + msg->BufferLength;
        }
        marshal_out(&m, &p);
        ULONG used = (ULONG)(m.Buffer - (uchar *)msg->Buffer);
        if (used > msg->BufferLength)
            NDR_RAISE(RPC_X_BAD_STUB_DATA);
        memset(m.Buffer, 0, msg->BufferLength - used);
        msg->BufferLength = used;
        free_params(&m, &p);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        fault = GetExceptionCode();
        faulted = TRUE;
    }
    NdrpContextRecordsFree(&m);
    NdrpCheckRelease(&m);
    if (m.FullPtrXlatTables)
        NdrFullPointerXlatFree(m.FullPtrXlatTables);
    if (p.OiFlags & Oi_RPCSS_ALLOC_USED)
        NdrRpcSsDisableAllocate(&m);
    arena_free(arena);
    HeapFree(GetProcessHeap(), 0, arena);
    if (frame != (uchar *)local_frame)
        HeapFree(GetProcessHeap(), 0, frame);
    (void)arena_owns;
    if (faulted)
        NDR_RAISE(fault);
    if (p.Ext && (p.Flags2 & (OIF2_HAS_NOTIFY | OIF2_HAS_NOTIFY2))) {
        const NDR_NOTIFY_ROUTINE routine = desc->NotifyRoutineTable[p.NotifyIndex];
        if (p.Flags2 & OIF2_HAS_NOTIFY2)
            ((NDR_NOTIFY2_ROUTINE)routine)(1);
        else
            routine();
    }
    return result;
}

RPCRTAPI void RPC_ENTRY NdrServerCall2(PRPC_MESSAGE msg)
{
    ULONG phase = 0;
    NdrStubCall2(nullptr, nullptr, msg, &phase);
}

RPCRTAPI LONG RPC_ENTRY NdrStubCall(IRpcStubBuffer *stub, IRpcChannelBuffer *channel, PRPC_MESSAGE msg, ULONG *phase)
{
    return NdrStubCall2(stub, channel, msg, phase);
}

RPCRTAPI void RPC_ENTRY NdrServerCall(PRPC_MESSAGE msg)
{
    NdrServerCall2(msg);
}
