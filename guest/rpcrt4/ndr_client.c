/*
 * the client side of the interpreter: NdrClientCall2 and the calls of proxies. A call finds its binding, sizes its buffer, fills
 * it with the [in] parameters, sends it, and reads the [out] parameters and the return value from the reply.
 */
#include "ndr_proc.h"
#include <stdarg.h>

static BOOL is_pointer_type(uchar fc)
{
    return (fc >= FC_RP && fc <= FC_FP) || fc == FC_IP || fc == FC_BIND_CONTEXT;
}

/* walks one parameter in the pass; the object a parameter names is in its slot, or is the slot. */
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

/* a simple type's value in the slot of a parameter passed by value. */
static void simple_in(PMIDL_STUB_MESSAGE m, NDR_PASS pass, uchar *slot, uchar fc)
{
    NDRC c = {m, pass, nullptr};
    NdrpSimple(&c, slot, fc);
}

/* the size of the parameters that need to be measured. */
static void size_params(PMIDL_STUB_MESSAGE m, const NDR_PROC *p)
{
    m->BufferLength = p->ClientSize;
    if (!(p->Oi2Flags & OIF_CLIENT_MUST_SIZE))
        return;
    for (ULONG i = 0; i < p->NumParams; i++) {
        NDR_PARAM prm;
        const uchar *d = p->Params + 6 * i;
        prm.Attrs = *(const USHORT *)d;
        prm.Offset = *(const USHORT *)(d + 2);
        prm.TypeOffset = *(const USHORT *)(d + 4);
        prm.BaseType = d[4];
        if ((prm.Attrs & (PA_MUST_SIZE | PA_IS_IN | PA_IS_PIPE)) != (PA_MUST_SIZE | PA_IS_IN))
            continue;
        uchar *slot = p->Args + prm.Offset;
        if (prm.Attrs & 0x800) {
            m->BufferLength = ((m->BufferLength + 3) & ~3u) + 4;
            continue;
        }
        if (prm.Attrs & PA_IS_BASETYPE) {
            if (prm.Attrs & PA_IS_SIMPLE_REF)
                slot = *(uchar **)slot;
            simple_in(m, NDR_SIZE, slot, prm.BaseType);
            continue;
        }
        param_walk(m, NDR_SIZE, p, &prm, slot);
    }
}

static void marshal_params(PMIDL_STUB_MESSAGE m, const NDR_PROC *p, BOOL promoted_floats)
{
    for (ULONG i = 0; i < p->NumParams; i++) {
        const uchar *d = p->Params + 6 * i;
        NDR_PARAM prm = {*(const USHORT *)d, *(const USHORT *)(d + 2), *(const USHORT *)(d + 4), d[4]};
        if ((prm.Attrs & (PA_IS_IN | PA_IS_PIPE)) != PA_IS_IN)
            continue;
        uchar *slot = p->Args + prm.Offset;
        if (prm.Attrs & 0x800) {
            NDRC c = {m, NDR_MARSHAL, nullptr};
            NdrpAlign(&c, 3);
            *(ULONG *)NdrpTake(&c, 4) = *(ULONG_PTR *)slot != 0;
            continue;
        }
        if (prm.Attrs & PA_IS_BASETYPE) {
            if (prm.Attrs & PA_IS_SIMPLE_REF)
                slot = *(uchar **)slot;
            else if (promoted_floats && prm.BaseType == FC_FLOAT)
                *(float *)slot = (float)*(double *)slot;
            if (prm.BaseType == FC_ENUM16 && (*(ULONG *)slot & 0xFFFF8000u))
                NDR_RAISE(RPC_X_ENUM_VALUE_OUT_OF_RANGE);
            simple_in(m, NDR_MARSHAL, slot, prm.BaseType);
            continue;
        }
        param_walk(m, NDR_MARSHAL, p, &prm, slot);
    }
}

static void unmarshal_params(PMIDL_STUB_MESSAGE m, const NDR_PROC *p, ULONG_PTR *ret)
{
    for (ULONG i = 0; i < p->NumParams; i++) {
        const uchar *d = p->Params + 6 * i;
        NDR_PARAM prm = {*(const USHORT *)d, *(const USHORT *)(d + 2), *(const USHORT *)(d + 4), d[4]};
        if (prm.Attrs & PA_IS_PIPE)
            continue;
        if (!(prm.Attrs & PA_IS_OUT))
            continue;
        uchar *slot = (prm.Attrs & PA_IS_RETURN) ? (uchar *)ret : p->Args + prm.Offset;
        if (prm.Attrs & PA_IS_BASETYPE) {
            if (prm.Attrs & PA_IS_SIMPLE_REF)
                slot = *(uchar **)slot;
            if (prm.BaseType == FC_INT3264 || prm.BaseType == FC_UINT3264) {
                simple_in(m, NDR_UNMARSHAL, slot, prm.BaseType);
                continue;
            }
            simple_in(m, NDR_UNMARSHAL, slot, prm.BaseType);
            continue;
        }
        param_walk(m, NDR_UNMARSHAL, p, &prm, slot);
    }
}

/* on failure, the [out] parameters hold nothing the caller could take for a result. */
static void zero_out(PMIDL_STUB_MESSAGE m, const NDR_PROC *p)
{
    for (ULONG i = 0; i < p->NumParams; i++) {
        const uchar *d = p->Params + 6 * i;
        NDR_PARAM prm = {*(const USHORT *)d, *(const USHORT *)(d + 2), *(const USHORT *)(d + 4), d[4]};
        if ((prm.Attrs & (PA_IS_OUT | PA_IS_IN | PA_IS_RETURN | PA_IS_PIPE)) != PA_IS_OUT)
            continue;
        uchar *slot = p->Args + prm.Offset;
        __try {
            if (prm.Attrs & PA_IS_BASETYPE) {
                if ((prm.Attrs & PA_IS_SIMPLE_REF) && *(uchar **)slot)
                    memset(*(uchar **)slot, 0, NdrSimpleMem[prm.BaseType]);
                continue;
            }
            PFORMAT_STRING type = p->Types + prm.TypeOffset;
            uchar *object = *(uchar **)slot;
            if (!object)
                continue;
            if (type[0] == FC_RP) {
                if (type[1] & FC_POINTER_DEREF) {
                    *(void **)object = nullptr;
                    continue;
                }
                if (type[1] & FC_SIMPLE_POINTER) {
                    memset(object, 0, NdrSimpleMem[type[2]]);
                    continue;
                }
                NDRC c = {m, NDR_SIZE, nullptr};
                memset(object, 0, NdrpMemorySize(&c, object, type + 2 + *(const short *)(type + 2)));
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
        }
    }
}

/* ------------------------------------------------------------------ binding */

/* the binding handle a call uses: a parameter, a context handle's, the interface's own. */
static RPC_BINDING_HANDLE call_handle(PMIDL_STUB_MESSAGE m, const NDR_PROC *p, void **generic_bound,
                                      const GENERIC_BINDING_ROUTINE_PAIR **generic_pair)
{
    PMIDL_STUB_DESC desc = (PMIDL_STUB_DESC)m->StubDesc;
    *generic_bound = nullptr;
    *generic_pair = nullptr;
    if (p->HandleType == 0) {
        PFORMAT_STRING h = p->HandleDesc;
        uchar *slot = p->Args + *(const USHORT *)(h + 2);
        switch (h[0]) {
        case FC_BIND_PRIMITIVE:
            if (h[1] & HANDLE_PARAM_IS_VIA_PTR)
                slot = *(uchar **)slot;
            return *(RPC_BINDING_HANDLE *)slot;
        case FC_BIND_CONTEXT: {
            if (h[1] & HANDLE_PARAM_IS_VIA_PTR)
                slot = *(uchar **)slot;
            NDR_CCONTEXT ctx = *(NDR_CCONTEXT *)slot;
            if (!ctx) {
                if (!(h[1] & NDR_CONTEXT_HANDLE_CANNOT_BE_NULL) && (h[1] & 0x20) == 0)
                    NDR_RAISE(RPC_X_SS_IN_NULL_CONTEXT);
                NDR_RAISE(RPC_X_SS_IN_NULL_CONTEXT);
            }
            RPC_BINDING_HANDLE b = NDRCContextBinding(ctx);
            if (!b)
                NDR_RAISE(ERROR_INVALID_HANDLE);
            return b;
        }
        case FC_BIND_GENERIC: {
            const GENERIC_BINDING_ROUTINE_PAIR *pair = &desc->aGenericBindingRoutinePairs[h[4]];
            uchar size = h[1] & 0x0F;
            ULONG_PTR value = 0;
            uchar *src = slot;
            if (h[1] & HANDLE_PARAM_IS_VIA_PTR)
                src = *(uchar **)src;
            switch (size) {
            case 1:
                value = *src;
                break;
            case 2:
                value = *(USHORT *)src;
                break;
            case 4:
                value = *(ULONG *)src;
                break;
            default:
                value = *(ULONG_PTR *)src;
            }
            void *bound = pair->pfnBind((void *)value);
            if (!bound)
                NDR_RAISE(RPC_S_INVALID_BINDING);
            *generic_bound = (void *)value;
            *generic_pair = pair;
            return bound;
        }
        default:
            NDR_RAISE(RPC_S_INTERNAL_ERROR);
        }
    }
    switch (p->HandleType) {
    case FC_BIND_PRIMITIVE:
        return *desc->IMPLICIT_HANDLE_INFO.pPrimitiveHandle;
    case FC_BIND_GENERIC: {
        PGENERIC_BINDING_INFO info = desc->IMPLICIT_HANDLE_INFO.pGenericBindingInfo;
        void *bound = info->pfnBind(info->pObj);
        if (!bound)
            NDR_RAISE(RPC_S_INVALID_BINDING);
        *generic_bound = info->pObj;
        *generic_pair = (const GENERIC_BINDING_ROUTINE_PAIR *)info;
        return bound;
    }
    case FC_AUTO_HANDLE:
        NDR_RAISE(RPC_S_NO_BINDINGS);
        return nullptr;
    default:
        NDR_RAISE(RPC_S_INTERNAL_ERROR);
        return nullptr;
    }
}

/* ------------------------------------------------------------------ the call */

/* the transport a call's buffer goes through: the rpc runtime, or the channel of a proxy. */
extern void NdrpChannelGetBuffer(PMIDL_STUB_MESSAGE m);
extern void NdrpChannelSendReceive(PMIDL_STUB_MESSAGE m);
extern void NdrpChannelFreeBuffer(PMIDL_STUB_MESSAGE m);
extern void NdrpProxyBegin(void *interface_pointer, PRPC_MESSAGE msg, PMIDL_STUB_MESSAGE m, PMIDL_STUB_DESC desc,
                           ULONG proc_num);
extern void NdrpProxyEnd(PMIDL_STUB_MESSAGE m);

static CLIENT_CALL_RETURN client_call(PMIDL_STUB_DESC desc, PFORMAT_STRING format, uchar *args, BOOL promoted_floats)
{
    NDR_PROC p;
    NdrpParseProc(format, &p);
    p.Types = desc->pFormatTypes;
    p.Args = args;

    RPC_MESSAGE msg;
    MIDL_STUB_MESSAGE m;
    CLIENT_CALL_RETURN result;
    ULONG_PTR ret[2] = {0, 0};
    memset(&msg, 0, sizeof(msg));
    result.Simple = 0;
    BOOL object = (p.OiFlags & Oi_OBJECT_PROC) != 0;

    msg.RpcInterfaceInformation = desc->RpcInterfaceInformation;
    msg.ProcNum = p.ProcNum;
    msg.DataRepresentation = NDR_LOCAL_DATA_REPRESENTATION;
    msg.RpcFlags = p.RpcFlags;
    NdrpInitStubMessage(&m, &msg, desc, TRUE, &p);
    m.StackTop = args;
    m.pContext = (struct _NDR_PROC_CONTEXT *)&p;

    void *generic_bound = nullptr;
    const GENERIC_BINDING_ROUTINE_PAIR *generic_pair = nullptr;
    ULONG exception = 0;
    BOOL failed = FALSE;
    BOOL proxied = FALSE;

    __try {
        if (p.OiFlags & Oi_FULL_PTR_USED)
            m.FullPtrXlatTables = NdrFullPointerXlatInit(0, XLAT_CLIENT);
        if (p.OiFlags & Oi_RPCSS_ALLOC_USED)
            NdrRpcSmSetClientToOsf(&m);
        RPC_BINDING_HANDLE handle = nullptr;
        if (object) {
            NdrpProxyBegin(*(void **)args, &msg, &m, desc, p.ProcNum);
            proxied = TRUE;
        } else {
            handle = call_handle(&m, &p, &generic_bound, &generic_pair);
            msg.Handle = handle;
        }
        size_params(&m, &p);
        if (object) {
            msg.BufferLength = m.BufferLength;
            NdrpChannelGetBuffer(&m);
        } else {
            NdrGetBuffer(&m, m.BufferLength, handle);
        }
        marshal_params(&m, &p, promoted_floats);
        if (object) {
            msg.BufferLength = (ULONG)(m.Buffer - (uchar *)msg.Buffer);
            NdrpChannelSendReceive(&m);
        } else {
            NdrSendReceive(&m, m.Buffer);
        }
        m.fIsIn = 0;
        unmarshal_params(&m, &p, ret);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        exception = GetExceptionCode();
        failed = TRUE;
    }
    __try {
        if (failed)
            zero_out(&m, &p);
        if (object) {
            if (proxied)
                NdrpChannelFreeBuffer(&m);
            if (proxied)
                NdrpProxyEnd(&m);
        } else {
            NdrFreeBuffer(&m);
        }
        if (m.FullPtrXlatTables)
            NdrFullPointerXlatFree(m.FullPtrXlatTables);
        if (generic_pair) {
            if (p.HandleType == 0)
                generic_pair->pfnUnbind(generic_bound, (uchar *)msg.Handle);
            else
                ((PGENERIC_BINDING_INFO)generic_pair)->pfnUnbind(generic_bound, (uchar *)msg.Handle);
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    if (failed) {
        if (object)
            return (CLIENT_CALL_RETURN){.Simple = (LONG_PTR)(LONG)NdrProxyErrorHandler(exception)};
        if (p.OiFlags & Oi_HAS_COMM_OR_FAULT) {
            /* the status goes to the parameters the interface named for it, or is raised if they cannot take it. */
            const COMM_FAULT_OFFSETS *offsets = &desc->CommFaultOffsets[p.ProcNum];
            ULONG *comm = offsets->CommOffset == -1 ? nullptr : *(ULONG **)(args + offsets->CommOffset);
            ULONG *fault = offsets->FaultOffset == -1 ? nullptr : *(ULONG **)(args + offsets->FaultOffset);
            if (offsets->CommOffset == -2)
                comm = nullptr;
            if (offsets->FaultOffset == -2)
                fault = nullptr;
            RPC_STATUS left = NdrMapCommAndFaultStatus(&m, comm, fault, (RPC_STATUS)exception);
            if (left)
                NDR_RAISE(left);
            return (CLIENT_CALL_RETURN){.Simple = 0};
        }
        NDR_RAISE(exception);
    }
    if (p.Oi2Flags & OIF_HAS_RETURN)
        result.Simple = (LONG_PTR)ret[0];
    return result;
}

RPCRTAPI CLIENT_CALL_RETURN RPC_VAR_ENTRY NdrClientCall2(PMIDL_STUB_DESC desc, PFORMAT_STRING format, ...)
{
    va_list ap;
    va_start(ap, format);
#ifdef _WIN64
    /* the arguments of the procedure are the variable ones of this call, floats widened to doubles. */
    uchar *args = (uchar *)ap;
    BOOL promoted = TRUE;
#else
    /* the stub passes the address of its own frame. */
    uchar *args = va_arg(ap, uchar *);
    BOOL promoted = FALSE;
#endif
    CLIENT_CALL_RETURN r = client_call(desc, format, args, promoted);
    va_end(ap);
    return r;
}
