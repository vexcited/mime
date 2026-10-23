/*
 * the ndr engine, private to rpcrt4.
 *
 * a walk goes over one object by its type format string in one of five passes: size the buffer, marshal, unmarshal,
 * measure the memory unmarshaling would take, or free what unmarshaling allocated. one walker serves all five since the
 * wire order does not depend on the pass. an embedded pointer's referent is written in place and its pointee is queued
 * until the outermost construct is done, which is the order midl stubs expect.
 */
#ifndef MIME_RPCRT4_NDR_H
#define MIME_RPCRT4_NDR_H

#include "rpcrt4_private.h"
#include <mime/win32/rpcndr.h>

typedef unsigned char uchar;

typedef enum {
    NDR_SIZE,
    NDR_MARSHAL,
    NDR_UNMARSHAL,
    NDR_MEMSIZE,
    NDR_FREE,
} NDR_PASS;

#define NDR_WIRE_READ(pass) ((pass) == NDR_UNMARSHAL || (pass) == NDR_MEMSIZE)

/* walk flags. */
#define NDRF_MUSTALLOC 0x01
/* the object is inside another, so a pointer's pointee waits in the queue. */
#define NDRF_EMBEDDED 0x02
/* an enclosing construct reserved the conformance counts. */
#define NDRF_CONF_DONE 0x04

/* padding a format's sizes may not account for. */
#define NDR_BUFFER_SLACK 8

/* the most one wire object can make the stub allocate. */
#define NDR_MAX_ALLOC 0x10000000u

/* stands in for object memory the measuring pass does not have. */
#define NDR_DUMMY ((uchar *)16)

#define NDR_MAX_DIMS 8

typedef struct NDR_QITEM {
    struct NDR_QITEM *Next;
    uchar **Slot;
    uchar *Pointer;
    PFORMAT_STRING Format;
    ULONG RefId;
    /* the enclosing structure, since a count of the pointee may be one of its fields. */
    uchar *Memory;
} NDR_QITEM;

typedef struct NDR_QCHUNK {
    struct NDR_QCHUNK *Next;
    NDR_QITEM Items[64];
} NDR_QCHUNK;

/* the state of the walk in progress, hung off pPointerQueueState. */
struct NDR_POINTER_QUEUE_STATE {
    NDR_PASS Pass;
    NDR_QITEM *Head;
    NDR_QITEM *Tail;
    NDR_QCHUNK *Chunks;
    ULONG Used;
    NDR_QCHUNK First;
    /* the full pointer ref id the next allocated pointee is filed under. */
    ULONG PendingRefId;
    /* the conformance counts an enclosing construct reserved, and which one the next array takes. */
    ULONG_PTR Conf[NDR_MAX_DIMS];
    ULONG *Slots;
    ULONG Dims;
    ULONG Next;
    ULONG Depth;
    /* replaces the referent a structure copy left in the next unmarshaled pointer's slot. */
    uchar *Preset;
    BOOL HasPreset;
    /* a structure's pointers from before a buffer copy overwrote them, for its pointer layout. */
    uchar *Old;
    /* freed with the walk. */
    struct NDR_SCRATCH *Scratch;
    /*
     * how many complex structures and arrays the walk is inside. embedded pointees wait, in order, for the end of the
     * outermost one. inside other constructs they follow their construct at once.
     */
    ULONG Regions;
    BOOL Draining;
};

typedef struct NDR_POINTER_QUEUE_STATE NDR_QUEUE;

typedef struct NDRC {
    PMIDL_STUB_MESSAGE M;
    NDR_PASS Pass;
    NDR_QUEUE *Q;
} NDRC;

#ifdef NDR_TRACE
/* reports where each status is raised. */
void NdrpTraceRaise(ULONG status, const char *file, int line);
#define NDR_RAISE(status) (NdrpTraceRaise((ULONG)(status), __FILE__, __LINE__), RpcRaiseException(status))
#else
#define NDR_RAISE(status) RpcRaiseException(status)
#endif

/* the referent goes at the cursor, rather than at an address in an object already copied. */
#define NDR_WIRE_CURSOR ((uchar *)1)

/* interface pointers in the buffer are marshaled by com. */
typedef struct {
    void (*Size)(NDRC *c, uchar *pointer, PFORMAT_STRING format);
    void (*Marshal)(NDRC *c, uchar *pointer, PFORMAT_STRING format);
    void (*Unmarshal)(NDRC *c, uchar **pointer, PFORMAT_STRING format);
    void (*Free)(NDRC *c, uchar *pointer, PFORMAT_STRING format);
    void (*Skip)(NDRC *c);
} NDR_OLE_HOOKS;

const NDR_OLE_HOOKS *NdrpOleHooks(void);

/* ------------------------------------------------------------------ ndr_core.c */

extern const uchar NdrSimpleAlign[0x100];
extern const uchar NdrSimpleWire[0x100];
extern const uchar NdrSimpleMem[0x100];

void *NdrpAlloc(PMIDL_STUB_MESSAGE m, SIZE_T size);
void NdrpFree(PMIDL_STUB_MESSAGE m, void *memory);
BOOL NdrpInBuffer(PMIDL_STUB_MESSAGE m, const void *memory);

/* ------------------------------------------------------------------ ndr_walk.c */

void NdrpWalkTop(PMIDL_STUB_MESSAGE m, NDR_PASS pass, uchar **memory, PFORMAT_STRING format, ULONG flags);
void NdrpWalk(NDRC *c, uchar **memory, PFORMAT_STRING format, ULONG flags);
uchar *NdrpAllocObject(NDRC *c, uchar **memory, SIZE_T size, ULONG flags);
ULONG NdrpMemorySize(NDRC *c, uchar *memory, PFORMAT_STRING format);
SIZE_T NdrpOutMemory(PMIDL_STUB_MESSAGE m, PFORMAT_STRING format);
ULONG_PTR NdrpComputeConformance(PMIDL_STUB_MESSAGE m, uchar *memory, PFORMAT_STRING descriptor);
ULONG_PTR NdrpComputeVariance(PMIDL_STUB_MESSAGE m, uchar *memory, PFORMAT_STRING descriptor);
void NdrpAlign(NDRC *c, ULONG mask);
void *NdrpScratch(NDRC *c, SIZE_T size);
void NdrpRegionEnter(NDRC *c);

/*
 * a count read from the wire must match the parameter or field it correlates with, or the stub would allocate for one
 * number and walk, free and marshal for another. a count depending on a parameter not read yet is checked once all are.
 */
void NdrpCheckCount(NDRC *c, uchar *memory, PFORMAT_STRING desc, ULONG_PTR wire);
void NdrpCheckVariance(NDRC *c, uchar *memory, PFORMAT_STRING desc, ULONG offset, ULONG actual);
void NdrpCheckSwitch(NDRC *c, uchar *memory, PFORMAT_STRING desc, LONG_PTR wire);
void NdrpCheckDeferred(PMIDL_STUB_MESSAGE m);
void NdrpCheckRelease(PMIDL_STUB_MESSAGE m);
void NdrpRegionLeave(NDRC *c);
uchar *NdrpTake(NDRC *c, ULONG count);
ULONG NdrpU32(NDRC *c, ULONG value);
void NdrpSimple(NDRC *c, uchar *memory, uchar fc);
void NdrpPointerAt(NDRC *c, uchar **slot, PFORMAT_STRING pointer_format, ULONG flags, uchar *wire);
ULONG NdrpCheckedProduct(ULONG_PTR a, ULONG_PTR b);

/* ------------------------------------------------------------------ ndr_type.c */

void NdrpStruct(NDRC *c, uchar **memory, PFORMAT_STRING format, ULONG flags);
void NdrpArray(NDRC *c, uchar **memory, PFORMAT_STRING format, ULONG flags);
void NdrpString(NDRC *c, uchar **memory, PFORMAT_STRING format, ULONG flags);
void NdrpUnion(NDRC *c, uchar **memory, PFORMAT_STRING format, ULONG flags);
void NdrpSpecial(NDRC *c, uchar **memory, PFORMAT_STRING format, ULONG flags);
void NdrpUserPointee(NDRC *c, uchar *object, PFORMAT_STRING format);
void NdrpQueueUser(NDRC *c, uchar *object, PFORMAT_STRING format);
void NdrpContextHandle(NDRC *c, uchar **memory, PFORMAT_STRING format);
void NdrpContextRecordsFree(PMIDL_STUB_MESSAGE m);

/* correlation descriptors of the new kind are longer than four bytes. */
#define NDR_DESC_SIZE(m) (4 + (m)->CorrDespIncrement)

#endif
