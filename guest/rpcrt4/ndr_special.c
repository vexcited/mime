/*
 * the types of the ndr engine that other code marshals: user marshaled types, which a routine of the stub writes, types
 * transmitted as another type, and ranges.
 */
#include "ndr.h"

/* ------------------------------------------------------------------ ranges */

static void range(NDRC *c, uchar **pm, PFORMAT_STRING f, ULONG flags)
{
    uchar base = f[1] & 0x0F;
    NdrpAllocObject(c, pm, NdrSimpleMem[base], flags);
    NdrpSimple(c, *pm, base);
    if (c->Pass != NDR_UNMARSHAL)
        return;
    LONG low = *(const LONG *)(f + 2), high = *(const LONG *)(f + 6);
    uchar *p = *pm;
    LONGLONG value;
    switch (base) {
    case FC_BYTE:
    case FC_CHAR:
    case FC_USMALL:
        value = *p;
        break;
    case FC_SMALL:
        value = *(signed char *)p;
        break;
    case FC_SHORT:
        value = *(short *)p;
        break;
    case FC_USHORT:
    case FC_WCHAR:
    case FC_ENUM16:
        value = *(USHORT *)p;
        break;
    case FC_LONG:
        value = *(LONG *)p;
        break;
    case FC_ULONG:
    case FC_ENUM32:
    case FC_ERROR_STATUS_T:
        value = *(ULONG *)p;
        if (value < (ULONG)low || value > (ULONG)high)
            NDR_RAISE(RPC_S_INVALID_BOUND);
        return;
    default:
        NDR_RAISE(RPC_S_INTERNAL_ERROR);
        return;
    }
    if (value < low || value > high)
        NDR_RAISE(RPC_S_INVALID_BOUND);
}

/* ------------------------------------------------------------------ user marshaled types */

static void user_flags(NDRC *c, USER_MARSHAL_CB *cb, PFORMAT_STRING f, USER_MARSHAL_CB_TYPE type)
{
    PMIDL_STUB_MESSAGE m = c->M;
    ULONG flags = m->dwDestContext & 0xFF;
    if (m->pAsyncMsg)
        flags |= USER_CALL_IS_ASYNC;
    if (m->CorrDespIncrement == 2)
        flags |= USER_CALL_NEW_CORRELATION_DESC;
    if (NDR_WIRE_READ(c->Pass))
        flags |= (m->RpcMsg ? m->RpcMsg->DataRepresentation : NDR_LOCAL_DATA_REPRESENTATION) << 16;
    cb->Flags = flags;
    cb->pStubMsg = m;
    cb->pReserve = (f[1] & USER_MARSHAL_IID) ? f + 10 : nullptr;
    cb->Signature = USER_MARSHAL_CB_SIGNATURE;
    cb->CBType = type;
    cb->pFormat = f;
    cb->pTypeFormat = f + 8 + *(const short *)(f + 8);
}

/* the routines of the type, picked by the index in the format. */
static const USER_MARSHAL_ROUTINE_QUADRUPLE *user_routines(NDRC *c, PFORMAT_STRING f)
{
    return &c->M->StubDesc->aUserMarshalQuadruple[*(const USHORT *)(f + 2)];
}

static void user_object(NDRC *c, uchar *memory, PFORMAT_STRING f, ULONG flags)
{
    PMIDL_STUB_MESSAGE m = c->M;
    const USER_MARSHAL_ROUTINE_QUADRUPLE *q = user_routines(c, f);
    ULONG wire_size = *(const USHORT *)(f + 6);
    USER_MARSHAL_CB cb;
    ULONG align_mask = f[1] & 0x0F;
    (void)flags;

    NdrpAlign(c, align_mask);
    switch (c->Pass) {
    case NDR_SIZE:
        if (wire_size) {
            NdrpTake(c, wire_size);
        } else {
            user_flags(c, &cb, f, USER_MARSHAL_CB_BUFFER_SIZE);
            m->BufferLength = q->pfnBufferSize(&cb.Flags, m->BufferLength, memory);
        }
        break;
    case NDR_MARSHAL:
        user_flags(c, &cb, f, USER_MARSHAL_CB_MARSHALL);
        m->Buffer = q->pfnMarshall(&cb.Flags, m->Buffer, memory);
        break;
    case NDR_UNMARSHAL:
        user_flags(c, &cb, f, USER_MARSHAL_CB_UNMARSHALL);
        m->Buffer = q->pfnUnmarshall(&cb.Flags, m->Buffer, memory);
        break;
    case NDR_MEMSIZE:
        if (!wire_size)
            NDR_RAISE(RPC_X_WRONG_STUB_VERSION);
        NdrpTake(c, wire_size);
        break;
    case NDR_FREE:
        user_flags(c, &cb, f, USER_MARSHAL_CB_FREE);
        q->pfnFree(&cb.Flags, memory);
        break;
    }
}

static void user_marshal(NDRC *c, uchar **pm, PFORMAT_STRING f, ULONG flags)
{
    ULONG kind = f[1] & USER_MARSHAL_POINTER;
    ULONG memory_size = *(const USHORT *)(f + 4);
    if (!kind) {
        /* the object itself is the user type; unmarshaling needs its memory. */
        NdrpAllocObject(c, pm, memory_size, flags);
        user_object(c, *pm, f, flags);
        return;
    }
    /* a pointer to the user type: a unique one says if it is there, and the object follows. */
    uchar *pointer = *pm;
    BOOL present = TRUE;
    NdrpAlign(c, 3);
    if (kind == USER_MARSHAL_UNIQUE || (kind == USER_MARSHAL_REF && (flags & NDRF_EMBEDDED))) {
        ULONG marker = 0x72657355;
        if (NDR_WIRE_READ(c->Pass)) {
            ULONG v = NdrpU32(c, 0);
            present = v != 0;
        } else {
            present = pointer != nullptr || kind == USER_MARSHAL_REF;
            NdrpU32(c, present ? marker : 0);
        }
    }
    if (!present) {
        if (c->Pass == NDR_UNMARSHAL)
            *pm = nullptr;
        return;
    }
    uchar *object = NDR_WIRE_READ(c->Pass) ? (c->Pass == NDR_UNMARSHAL ? *pm : nullptr) : pointer;
    if (NDR_WIRE_READ(c->Pass))
        NdrpAllocObject(c, &object, memory_size, 0);
    user_object(c, object, f, flags);
    if (c->Pass == NDR_UNMARSHAL)
        *pm = object;
    if (c->Pass == NDR_FREE && object)
        NdrpFree(c->M, object);
}

/* ------------------------------------------------------------------ transmitted types */

/*
 * a type that goes over the wire as another: the stub's routines make the transmitted form of the presented one on the way
 * out and the presented form of the transmitted one on the way in.
 */
static void transmit_as(NDRC *c, uchar **pm, PFORMAT_STRING f, ULONG flags)
{
    PMIDL_STUB_MESSAGE m = c->M;
    const XMIT_ROUTINE_QUINTUPLE *q = &m->StubDesc->aXmitQuintuple[*(const USHORT *)(f + 2)];
    ULONG presented_size = *(const USHORT *)(f + 4);
    PFORMAT_STRING xmit = f + 8 + *(const short *)(f + 8);
    uchar *saved_presented = m->pPresentedType;
    uchar *saved_transmit = m->pTransmitType;

    NdrpAlign(c, f[1] & 0x0F);
    switch (c->Pass) {
    case NDR_SIZE:
    case NDR_MARSHAL: {
        m->pPresentedType = *pm;
        m->pTransmitType = nullptr;
        q->pfnTranslateToXmit(m);
        uchar *object = m->pTransmitType;
        NdrpWalk(c, &object, xmit, 0);
        m->pTransmitType = object;
        q->pfnFreeXmit(m);
        break;
    }
    case NDR_UNMARSHAL: {
        uchar *object = nullptr;
        NdrpWalk(c, &object, xmit, NDRF_MUSTALLOC);
        uchar *presented = NdrpAllocObject(c, pm, presented_size, flags);
        m->pPresentedType = presented;
        m->pTransmitType = object;
        q->pfnTranslateFromXmit(m);
        q->pfnFreeXmit(m);
        break;
    }
    case NDR_MEMSIZE: {
        uchar *object = nullptr;
        NdrpWalk(c, &object, xmit, NDRF_MUSTALLOC);
        NdrpAllocObject(c, pm, presented_size, flags);
        break;
    }
    case NDR_FREE:
        m->pPresentedType = *pm;
        q->pfnFreeInst(m);
        break;
    }
    m->pPresentedType = saved_presented;
    m->pTransmitType = saved_transmit;
}

/* ------------------------------------------------------------------ entry */

void NdrpSpecial(NDRC *c, uchar **pm, PFORMAT_STRING f, ULONG flags)
{
    switch (f[0]) {
    case FC_RANGE:
        range(c, pm, f, flags);
        break;
    case FC_USER_MARSHAL:
        user_marshal(c, pm, f, flags);
        break;
    case FC_TRANSMIT_AS:
    case FC_REPRESENT_AS:
        transmit_as(c, pm, f, flags);
        break;
    case FC_BIND_CONTEXT:
        NdrpContextHandle(c, pm, f);
        break;
    case FC_BLKHOLE:
        /* a hole before a string: the string follows. */
        NdrpWalk(c, pm, f + 2 + *(const short *)(f + 2), flags);
        break;
    default:
        NDR_RAISE(RPC_X_WRONG_STUB_VERSION);
    }
}
