#include "../../ntos.h"
#include <seh/x64seh.h>

#define SEH_EXPORT
#define STATUS_INTEGER_DIVIDE_BY_ZERO ((NTSTATUS)0xC0000094L)
#define STATUS_INTEGER_OVERFLOW ((NTSTATUS)0xC0000095L)
#define STATUS_ARRAY_BOUNDS_EXCEEDED ((NTSTATUS)0xC000008CL)
#define STATUS_FLOAT_MULTIPLE_TRAPS ((NTSTATUS)0xC00002B5L)
#define STATUS_SINGLE_STEP_ ((NTSTATUS)0x80000004L)
#define STATUS_BREAKPOINT_ ((NTSTATUS)0x80000003L)
#define STATUS_ILLEGAL_INSTRUCTION_ ((NTSTATUS)0xC000001DL)

/* the unwind data of KiTrapCommon in entry.S hardcodes where its machine frame is. */
MIME_STATIC_ASSERT(KTRAP_ERROR + 0x20 == 0x1B0, "KiTrapCommon unwind data");
MIME_STATIC_ASSERT(KTRAP_RBX == 0x118 && KTRAP_RBP == 0x120 && KTRAP_RSI == 0x128 && KTRAP_RDI == 0x130 &&
                       KTRAP_R12 == 0x158 && KTRAP_R15 == 0x170,
                   "KiTrapCommon unwind data");

/* what drivers compare user pointers against, as windows exports them. */
ULONG_PTR MmUserProbeAddress = 0x7FFFFFFF0000;
PVOID MmHighestUserAddress = (PVOID)0x7FFFFFFEFFFF;
PVOID MmSystemRangeStart = (PVOID)0xFFFF800000000000;

void KiRestoreKernelContext(PCONTEXT context);
void NTAPI RtlCaptureContext(PCONTEXT context);
void NTAPI RtlRaiseException(PEXCEPTION_RECORD record);
EXCEPTION_DISPOSITION NTAPI RtlpExecuteHandlerForUnwind(PEXCEPTION_RECORD record, PVOID frame, PCONTEXT context,
                                                        PDISPATCHER_CONTEXT dispatcher, PEXCEPTION_ROUTINE handler);

/* the unwind data of the kernel and of the drivers it loaded. */
PVOID NTAPI RtlLookupFunctionEntry(ULONG64 pc, PULONG64 image_base, PVOID history)
{
    PKLDR_DATA_TABLE_ENTRY m = MiLookupModule(pc);
    if (!m)
        return nullptr;
    ULONG64 base = (ULONG64)m->DllBase;
    *image_base = base;
    if (!m->ExceptionTable)
        return nullptr;
    return search_table(m->ExceptionTable, m->ExceptionTableSize / sizeof(RUNTIME_FUNCTION), (ULONG)(pc - base), base);
}

/* a frame of the current thread's kernel stack, which may have grown below its first limit. */
static BOOLEAN frame_in_stack(ULONG64 frame)
{
    PKTHREAD thread = KiCurrentThread();
    if (!thread || (frame & 7))
        return FALSE;
    ULONG64 top = (ULONG64)thread->InitialStack, limit = (ULONG64)thread->StackLimit;
    return frame < top && frame >= limit - 0x100000;
}

void NTAPI RtlRestoreContext(PCONTEXT context, PEXCEPTION_RECORD record)
{
    (void)record;
    KiRestoreKernelContext(context);
}

DECLSPEC_NORETURN void NTAPI RtlRaiseStatus(NTSTATUS status)
{
    EXCEPTION_RECORD record = {};
    record.ExceptionCode = status;
    record.ExceptionFlags = EXCEPTION_NONCONTINUABLE;
    RtlRaiseException(&record);
    KeBugCheckEx(0x0000001E, (ULONG_PTR)status, 0, 0, 0);
}

#include <seh/x64unwind.inc>

/* kernel mode has no vectored handlers: the frames decide. */
BOOLEAN NTAPI RtlDispatchException(PEXCEPTION_RECORD record, PCONTEXT context)
{
    return RtlDispatchFrameException(record, context);
}

/* RtlRaiseException's second half, with the caller's context. */
void KiRaiseKernelException(PEXCEPTION_RECORD record, PCONTEXT context)
{
    if (RtlDispatchFrameException(record, context))
        RtlRestoreContext(context, nullptr);
    KeBugCheckEx(0x0000001E, (ULONG_PTR)record->ExceptionCode, (ULONG_PTR)record->ExceptionAddress,
                 record->ExceptionInformation[0], record->ExceptionInformation[1]); /* KMODE_EXCEPTION_NOT_HANDLED */
}

DECLSPEC_NORETURN void NTAPI ExRaiseStatus(NTSTATUS status)
{
    RtlRaiseStatus(status);
}

DECLSPEC_NORETURN void NTAPI ExRaiseAccessViolation(void)
{
    RtlRaiseStatus(STATUS_ACCESS_VIOLATION);
}

DECLSPEC_NORETURN void NTAPI ExRaiseDatatypeMisalignment(void)
{
    RtlRaiseStatus(STATUS_DATATYPE_MISALIGNMENT);
}

void NTAPI ProbeForRead(const volatile void *address, SIZE_T length, ULONG alignment)
{
    if (!length)
        return;
    if ((ULONG_PTR)address & (alignment - 1))
        ExRaiseDatatypeMisalignment();
    ULONG_PTR end = (ULONG_PTR)address + length;
    if (end < (ULONG_PTR)address || end > MmUserProbeAddress)
        ExRaiseAccessViolation();
}

/* every page of the range is written once, so a page that cannot be raises here. */
void NTAPI ProbeForWrite(volatile void *address, SIZE_T length, ULONG alignment)
{
    ProbeForRead(address, length, alignment);
    if (!length)
        return;
    volatile UCHAR *p = address;
    ULONG_PTR end = (ULONG_PTR)address + length - 1;
    for (;;) {
        *p = *p;
        ULONG_PTR next = ((ULONG_PTR)p & ~(ULONG_PTR)(PAGE_SIZE - 1)) + PAGE_SIZE;
        if (next > end)
            break;
        p = (volatile UCHAR *)next;
    }
}

static void frame_to_context(PKTRAP_FRAME frame, PCONTEXT c)
{
    memset(c, 0, sizeof(*c));
    c->ContextFlags = CONTEXT_FULL;
    c->Rax = frame->Rax;
    c->Rcx = frame->Rcx;
    c->Rdx = frame->Rdx;
    c->Rbx = frame->Rbx;
    c->Rsp = frame->Rsp;
    c->Rbp = frame->Rbp;
    c->Rsi = frame->Rsi;
    c->Rdi = frame->Rdi;
    c->R8 = frame->R8;
    c->R9 = frame->R9;
    c->R10 = frame->R10;
    c->R11 = frame->R11;
    c->R12 = frame->R12;
    c->R13 = frame->R13;
    c->R14 = frame->R14;
    c->R15 = frame->R15;
    c->Rip = frame->Rip;
    c->EFlags = (ULONG)frame->Rflags;
    c->SegCs = (USHORT)frame->Cs;
    c->SegSs = (USHORT)frame->Ss;
    c->SegDs = c->SegEs = KGDT64_R3_DATA | RPL_USER;
    c->SegFs = KGDT64_R3_CMTEB | RPL_USER;
    c->SegGs = KGDT64_R3_DATA | RPL_USER;
    c->MxCsr = frame->MxCsr;
    memcpy(&c->Xmm0, frame->Xmm, sizeof(frame->Xmm));
}

static void context_to_frame(const CONTEXT *c, PKTRAP_FRAME frame)
{
    frame->Rax = c->Rax;
    frame->Rcx = c->Rcx;
    frame->Rdx = c->Rdx;
    frame->Rbx = c->Rbx;
    frame->Rsp = c->Rsp;
    frame->Rbp = c->Rbp;
    frame->Rsi = c->Rsi;
    frame->Rdi = c->Rdi;
    frame->R8 = c->R8;
    frame->R9 = c->R9;
    frame->R10 = c->R10;
    frame->R11 = c->R11;
    frame->R12 = c->R12;
    frame->R13 = c->R13;
    frame->R14 = c->R14;
    frame->R15 = c->R15;
    frame->Rip = c->Rip;
    frame->Rflags = (c->EFlags & 0xDD5) | (frame->Rflags & ~0xDD5ull);
    memcpy(frame->Xmm, &c->Xmm0, sizeof(frame->Xmm));
}

/*
 * a fault in kernel mode that no fault recovery covers goes to the handlers of the frames it happened in, as on
 * windows. the code says whether a handler took it: an __except block never comes back here, it unwinds to itself.
 */
BOOLEAN KiDispatchKernelTrap(PKTRAP_FRAME frame)
{
    /* only code of an image, a driver's or the kernel's, reaches handlers; a leaf function has no unwind data. */
    if (!MiLookupModule(frame->Rip))
        return FALSE;
    EXCEPTION_RECORD record = {};
    record.ExceptionAddress = (PVOID)frame->Rip;
    switch (frame->Vector) {
    case 0:
        record.ExceptionCode = STATUS_INTEGER_DIVIDE_BY_ZERO;
        break;
    case 1:
        record.ExceptionCode = STATUS_SINGLE_STEP_;
        break;
    case 3:
        /* the trap points past the int3, the exception at it. */
        record.ExceptionCode = STATUS_BREAKPOINT_;
        frame->Rip--;
        record.ExceptionAddress = (PVOID)frame->Rip;
        record.NumberParameters = 1;
        break;
    case 4:
        record.ExceptionCode = STATUS_INTEGER_OVERFLOW;
        break;
    case 5:
        record.ExceptionCode = STATUS_ARRAY_BOUNDS_EXCEEDED;
        break;
    case 6:
        record.ExceptionCode = STATUS_ILLEGAL_INSTRUCTION_;
        break;
    case 13:
        record.ExceptionCode = STATUS_ACCESS_VIOLATION;
        record.NumberParameters = 2;
        record.ExceptionInformation[1] = (ULONG_PTR)-1;
        break;
    case 14:
        record.ExceptionCode = STATUS_ACCESS_VIOLATION;
        record.NumberParameters = 2;
        record.ExceptionInformation[0] = (frame->ErrorCode & 0x10) ? 8 : (frame->ErrorCode & 0x2) ? 1 : 0;
        record.ExceptionInformation[1] = frame->Cr2;
        break;
    case 16:
    case 19:
        record.ExceptionCode = STATUS_FLOAT_MULTIPLE_TRAPS;
        break;
    case 17:
        record.ExceptionCode = STATUS_DATATYPE_MISALIGNMENT;
        break;
    default:
        return FALSE;
    }
    CONTEXT context;
    frame_to_context(frame, &context);
    if (KI_TRAP_INTERRUPTS_ENABLED(frame))
        KiEnableInterrupts();
    if (!RtlDispatchFrameException(&record, &context))
        return FALSE;
    context_to_frame(&context, frame);
    return TRUE;
}

void NTAPI RtlUnwind(PVOID target_frame, PVOID target_ip, PEXCEPTION_RECORD record, PVOID value)
{
    CONTEXT context;
    RtlUnwindEx(target_frame, target_ip, record, value, &context, nullptr);
}
