/*
 * the ndr engine, private to rpcrt4.
 *
 * A walk goes over one object by its type format string in one of five passes: it sizes the buffer the object needs,
 * marshals it into the buffer, unmarshals it from the buffer, measures the memory unmarshaling would take, or frees what
 * unmarshaling allocated. One walker serves the five, because the order of what is on the wire does not depend on
 * what the pass does with it. Pointers inside an object are written where they stand and their referents are queued,
 * and the queue runs when the outermost construct is done, which is the order midl's stubs expect.
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

/* flags of a walk. */
#define NDRF_MUSTALLOC 0x01
/* the object is inside another: for a pointer, its pointee waits in the queue. */
#define NDRF_EMBEDDED 0x02
/* an enclosing construct reserved the counts of conformance. */
#define NDRF_CONF_DONE 0x04

/* the bytes added to the size of a buffer, for the alignment that the sizes in a format may not count. */
#define NDR_BUFFER_SLACK 8

/* the most one object of the wire can ask the stub to allocate. */
#define NDR_MAX_ALLOC 0x10000000u

/* the memory of an object that the pass measuring does not have. */
#define NDR_DUMMY ((uchar *)16)

/* the most dimensions of conformance one construct carries. */
#define NDR_MAX_DIMS 8

typedef struct NDR_QITEM {
    struct NDR_QITEM *Next;
    uchar **Slot;
    uchar *Pointer;
    PFORMAT_STRING Format;
    ULONG RefId;
    /* the structure the pointer was in, which a count of the pointee may be a field of. */
    uchar *Memory;
} NDR_QITEM;

typedef struct NDR_QCHUNK {
    struct NDR_QCHUNK *Next;
    NDR_QITEM Items[64];
} NDR_QCHUNK;

/* the state of the walk in progress, hanging from pPointerQueueState. */
struct NDR_POINTER_QUEUE_STATE {
    NDR_PASS Pass;
    NDR_QITEM *Head;
    NDR_QITEM *Tail;
    NDR_QCHUNK *Chunks;
    ULONG Used;
    NDR_QCHUNK First;
    /* the id under which the next pointee allocated is filed, for a full pointer. */
    ULONG PendingRefId;
    /* the counts of conformance an enclosing construct reserved, and which one the next array takes. */
    ULONG_PTR Conf[NDR_MAX_DIMS];
    ULONG *Slots;
    ULONG Dims;
    ULONG Next;
    ULONG Depth;
    /* the value the next pointer unmarshaled takes in its slot, instead of the referent a structure copy left there. */
    uchar *Preset;
    BOOL HasPreset;
    /* the pointers a structure had in memory before a copy from the buffer overwrote them, for its pointer layout. */
    uchar *Old;
    /* memory that lives as long as the walk. */
    struct NDR_SCRATCH *Scratch;
    /*
     * how many complex structures and arrays the walk is inside. The pointees of embedded pointers wait for the end of the
     * outermost of those, in order; inside the other constructs they follow the construct they are in at once.
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
/* a build with NDR_TRACE defined reports where each status is raised. */
void NdrpTraceRaise(ULONG status, const char *file, int line);
#define NDR_RAISE(status) (NdrpTraceRaise((ULONG)(status), __FILE__, __LINE__), RpcRaiseException(status))
#else
#define NDR_RAISE(status) RpcRaiseException(status)
#endif

/* where a pointer's referent stands on the wire: at the cursor, or at an address in the object copied already. */
#define NDR_WIRE_CURSOR ((uchar *)1)

/*
 * the interface pointers of the buffer, which com marshals: sizing and marshaling one, unmarshaling it, releasing it, and
 * skipping it while measuring memory.
 */
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
 * counts read from the wire must be the ones the parameters or fields they are correlated with say, or the stub would
 * allocate for one number and walk, free and marshal for another. A count that depends on a parameter that is not read yet is
 * checked when all of them are.
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
void NdrpContextHandle(NDRC *c, uchar **memory, PFORMAT_STRING format);
void NdrpContextRecordsFree(PMIDL_STUB_MESSAGE m);

/* the descriptors of correlation are this much longer than four bytes when a procedure has the new kind. */
#define NDR_DESC_SIZE(m) (4 + (m)->CorrDespIncrement)

#endif
