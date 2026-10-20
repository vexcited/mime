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

/*
 * What the routines of a type put on the wire when that is a pointer to something: the stub puts the reference of the pointer
 * at the place of the object and the routines put what it points to, which for an object inside another one comes after
 * that object, as the pointees of the pointers in it do. The object in memory is the same either way.
 */
void NdrpUserPointee(NDRC *c, uchar *object, PFORMAT_STRING f)
{
    user_object(c, object, f, 0);
}

static void user_marshal(NDRC *c, uchar **pm, PFORMAT_STRING f, ULONG flags)
{
    ULONG kind = f[1] & USER_MARSHAL_POINTER;
    ULONG memory_size = *(const USHORT *)(f + 4);
    PMIDL_STUB_MESSAGE m = c->M;
    /* the object itself is the user type; unmarshaling needs its memory. */
    NdrpAllocObject(c, pm, memory_size, flags);
    if (!kind) {
        user_object(c, *pm, f, flags);
        return;
    }
    NdrpAlign(c, 3);
    switch (c->Pass) {
    case NDR_SIZE:
        NdrpTake(c, 4);
        break;
    case NDR_MARSHAL:
        NdrpU32(c, 0x20000 + 4 * m->UniquePtrCount++);
        break;
    case NDR_UNMARSHAL:
    case NDR_MEMSIZE:
        /* the reference says nothing: the object is there whatever it is. */
        NdrpU32(c, 0);
        break;
    default:
        break;
    }
    if ((flags & NDRF_EMBEDDED) && c->Pass != NDR_FREE && (c->Q->Regions || c->Q->Draining || c->Q->Depth > 64))
        NdrpQueueUser(c, *pm, f);
    else
        user_object(c, *pm, f, flags);
}

/* ------------------------------------------------------------------ transmitted types */

/*
 * a type that goes over the wire as another: the stub's routines make the transmitted form of the presented one on the way
 * out and the presented form of the transmitted one on the way in.
 */
/*
 * walks the transmitted object by itself: its pointers and their pointees are done before the walk returns, since the object
 * is gone as soon as the stub's routines have freed it.
 */
static void walk_isolated(PMIDL_STUB_MESSAGE m, NDR_PASS pass, uchar **memory, PFORMAT_STRING f, ULONG flags)
{
    NDR_QUEUE *saved = m->pPointerQueueState;
    m->pPointerQueueState = nullptr;
    __try {
        NdrpWalkTop(m, pass, memory, f, flags);
    }
    __finally {
        m->pPointerQueueState = saved;
    }
}

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
        walk_isolated(m, c->Pass, &object, xmit, 0);
        m->pTransmitType = object;
        q->pfnFreeXmit(m);
        break;
    }
    case NDR_UNMARSHAL: {
        uchar *object = nullptr;
        walk_isolated(m, NDR_UNMARSHAL, &object, xmit, NDRF_MUSTALLOC);
        uchar *presented = NdrpAllocObject(c, pm, presented_size, flags);
        m->pPresentedType = presented;
        m->pTransmitType = object;
        q->pfnTranslateFromXmit(m);
        /* what the transmitted object holds is the stub's to free, not the routines'. */
        walk_isolated(m, NDR_FREE, &object, xmit, 0);
        if (!NdrpInBuffer(m, object))
            NdrpFree(m, object);
        break;
    }
    case NDR_MEMSIZE: {
        uchar *object = nullptr;
        walk_isolated(m, NDR_MEMSIZE, &object, xmit, NDRF_MUSTALLOC);
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
