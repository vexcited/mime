/*
 * the entry points of the ndr engine for each type: sizing, marshaling, unmarshaling, measuring and freeing, which midl's
 * stubs and programs call one type at a time. Each starts a walk of the one pass.
 */
#include "ndr.h"

#define TYPE_FUNCTIONS(Name, fc_ok)                                                                                 \
    RPCRTAPI unsigned char *RPC_ENTRY Ndr##Name##Marshall(PMIDL_STUB_MESSAGE m, unsigned char *memory,              \
                                                           PFORMAT_STRING format)                                   \
    {                                                                                                               \
        NdrpWalkTop(m, NDR_MARSHAL, &memory, format, 0);                                                            \
        return nullptr;                                                                                             \
    }                                                                                                               \
    RPCRTAPI void RPC_ENTRY Ndr##Name##BufferSize(PMIDL_STUB_MESSAGE m, unsigned char *memory,                      \
                                                   PFORMAT_STRING format)                                           \
    {                                                                                                               \
        NdrpWalkTop(m, NDR_SIZE, &memory, format, 0);                                                               \
    }                                                                                                               \
    RPCRTAPI unsigned char *RPC_ENTRY Ndr##Name##Unmarshall(PMIDL_STUB_MESSAGE m, unsigned char **memory,           \
                                                             PFORMAT_STRING format, unsigned char must_alloc)       \
    {                                                                                                               \
        NdrpWalkTop(m, NDR_UNMARSHAL, memory, format, must_alloc ? NDRF_MUSTALLOC : 0);                             \
        return nullptr;                                                                                             \
    }                                                                                                               \
    RPCRTAPI ULONG RPC_ENTRY Ndr##Name##MemorySize(PMIDL_STUB_MESSAGE m, PFORMAT_STRING format)                     \
    {                                                                                                               \
        uchar *memory = nullptr;                                                                                    \
        NdrpWalkTop(m, NDR_MEMSIZE, &memory, format, 0);                                                            \
        return m->MemorySize;                                                                                       \
    }                                                                                                               \
    RPCRTAPI void RPC_ENTRY Ndr##Name##Free(PMIDL_STUB_MESSAGE m, unsigned char *memory, PFORMAT_STRING format)     \
    {                                                                                                               \
        NdrpWalkTop(m, NDR_FREE, &memory, format, 0);                                                               \
    }

TYPE_FUNCTIONS(Pointer, 0)
TYPE_FUNCTIONS(SimpleStruct, 0)
TYPE_FUNCTIONS(ConformantStruct, 0)
TYPE_FUNCTIONS(ConformantVaryingStruct, 0)
TYPE_FUNCTIONS(ComplexStruct, 0)
TYPE_FUNCTIONS(FixedArray, 0)
TYPE_FUNCTIONS(ConformantArray, 0)
TYPE_FUNCTIONS(ConformantVaryingArray, 0)
TYPE_FUNCTIONS(VaryingArray, 0)
TYPE_FUNCTIONS(ComplexArray, 0)
TYPE_FUNCTIONS(NonConformantString, 0)
TYPE_FUNCTIONS(ConformantString, 0)
TYPE_FUNCTIONS(EncapsulatedUnion, 0)
TYPE_FUNCTIONS(NonEncapsulatedUnion, 0)
TYPE_FUNCTIONS(XmitOrRepAs, 0)
TYPE_FUNCTIONS(UserMarshal, 0)
TYPE_FUNCTIONS(InterfacePointer, 0)
TYPE_FUNCTIONS(ByteCountPointer, 0)

RPCRTAPI unsigned char *RPC_ENTRY NdrRangeUnmarshall(PMIDL_STUB_MESSAGE m, unsigned char **memory, PFORMAT_STRING format,
                                                     unsigned char must_alloc)
{
    NdrpWalkTop(m, NDR_UNMARSHAL, memory, format, must_alloc ? NDRF_MUSTALLOC : 0);
    return nullptr;
}

/* the generic entry points: the type is whatever the format says. */
RPCRTAPI void RPC_ENTRY NdrTypeMarshall(PMIDL_STUB_MESSAGE m, unsigned char *memory, PFORMAT_STRING format)
{
    NdrpWalkTop(m, NDR_MARSHAL, &memory, format, 0);
}

RPCRTAPI void RPC_ENTRY NdrTypeUnmarshall(PMIDL_STUB_MESSAGE m, unsigned char **memory, PFORMAT_STRING format)
{
    NdrpWalkTop(m, NDR_UNMARSHAL, memory, format, 0);
}

RPCRTAPI void RPC_ENTRY NdrTypeSize(PMIDL_STUB_MESSAGE m, unsigned char *memory, PFORMAT_STRING format)
{
    NdrpWalkTop(m, NDR_SIZE, &memory, format, 0);
}

RPCRTAPI void RPC_ENTRY NdrTypeFree(PMIDL_STUB_MESSAGE m, unsigned char *memory, PFORMAT_STRING format)
{
    NdrpWalkTop(m, NDR_FREE, &memory, format, 0);
}

/* ------------------------------------------------------------------ simple types */

RPCRTAPI void RPC_ENTRY NdrSimpleTypeMarshall(PMIDL_STUB_MESSAGE m, unsigned char *memory, unsigned char fc)
{
    NDRC c = {m, NDR_MARSHAL, nullptr};
    NdrpSimple(&c, memory, fc);
}

RPCRTAPI void RPC_ENTRY NdrSimpleTypeUnmarshall(PMIDL_STUB_MESSAGE m, unsigned char *memory, unsigned char fc)
{
    NDRC c = {m, NDR_UNMARSHAL, nullptr};
    NdrpSimple(&c, memory, fc);
}

RPCRTAPI unsigned char *RPC_ENTRY NdrUnmarshallBasetypeInline(PMIDL_STUB_MESSAGE m, unsigned char *memory,
                                                              unsigned char fc)
{
    NDRC c = {m, NDR_UNMARSHAL, nullptr};
    NdrpSimple(&c, memory, fc);
    return nullptr;
}

/* converts a simple type on the buffer in place to the form the stub would give it: a 16 bit enum to 32 bits. */
RPCRTAPI unsigned char *RPC_ENTRY NdrUserMarshalSimpleTypeConvert(ULONG *flags, unsigned char *buffer,
                                                                  unsigned char fc)
{
    /* the data representation of the call is the local one, so the value is as it stands. */
    buffer = (unsigned char *)(((ULONG_PTR)buffer + NdrSimpleAlign[fc]) & ~(ULONG_PTR)NdrSimpleAlign[fc]);
    return buffer + NdrSimpleWire[fc];
}

/* ------------------------------------------------------------------ user marshal info */

RPCRTAPI RPC_STATUS RPC_ENTRY NdrGetUserMarshalInfo(ULONG *flags, ULONG level, NDR_USER_MARSHAL_INFO *info)
{
    USER_MARSHAL_CB *cb = (USER_MARSHAL_CB *)flags;
    if (!flags || !info || level != 1 || cb->Signature != USER_MARSHAL_CB_SIGNATURE)
        return RPC_S_INVALID_ARG;
    PMIDL_STUB_MESSAGE m = cb->pStubMsg;
    memset(&info->Level1, 0, sizeof(info->Level1));
    info->InformationLevel = 1;
    info->Level1.Buffer = m->Buffer;
    info->Level1.pfnAllocate = m->pfnAllocate;
    info->Level1.pfnFree = m->pfnFree;
    info->Level1.pRpcChannelBuffer = m->pRpcChannelBuffer;
    return RPC_S_OK;
}

/* the structure the engine keeps for an unmarshaling: what it must not free as it is the receive buffer's. */
RPCRTAPI void RPC_ENTRY NdrCorrelationInitialize(PMIDL_STUB_MESSAGE m, void *memory, ULONG cache_size, ULONG flags)
{
}

RPCRTAPI void RPC_ENTRY NdrCorrelationPass(PMIDL_STUB_MESSAGE m)
{
}

RPCRTAPI void RPC_ENTRY NdrCorrelationFree(PMIDL_STUB_MESSAGE m)
{
}
