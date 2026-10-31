/*
 * the report of a failed stack cookie check: the process ends through the unhandled exception filter with
 * STATUS_STACK_BUFFER_OVERRUN and the context the check ran in, or at once through the fast fail interrupt
 * where the processor feature says it exists.
 */
#include "msvcrt_private.h"

#if defined(__x86_64__) || defined(__i386__)

#define FAST_FAIL_STACK_COOKIE_CHECK_FAILURE 2

NTSYSAPI PVOID NTAPI RtlVirtualUnwind(ULONG type, ULONG64 base, ULONG64 pc, PVOID function, PCONTEXT context,
                                      PVOID *handler_data, PULONG64 frame, PVOID pointers);

static EXCEPTION_RECORD g_record;
static CONTEXT g_context;
static EXCEPTION_POINTERS g_pointers = {&g_record, &g_context};

static DECLSPEC_NORETURN void raise_security_failure(void)
{
    SetUnhandledExceptionFilter(nullptr);
    UnhandledExceptionFilter(&g_pointers);
    TerminateProcess(GetCurrentProcess(), (UINT)STATUS_STACK_BUFFER_OVERRUN);
    for (;;)
        ;
}

static void fill_record(ULONG_PTR address)
{
    g_record.ExceptionCode = STATUS_STACK_BUFFER_OVERRUN;
    g_record.ExceptionFlags = EXCEPTION_NONCONTINUABLE;
    g_record.ExceptionAddress = (PVOID)address;
    g_record.NumberParameters = 1;
    g_record.ExceptionInformation[0] = FAST_FAIL_STACK_COOKIE_CHECK_FAILURE;
}

static DECLSPEC_NORETURN void fast_fail(void)
{
    __asm__ volatile("int $0x29" : : "c"(FAST_FAIL_STACK_COOKIE_CHECK_FAILURE) : "memory");
    __builtin_unreachable();
}

#ifdef _WIN64

/* the context of the function that called the failing one. */
static void capture_previous_context(CONTEXT *context)
{
    RtlCaptureContext(context);
    for (int frame = 0; frame < 2; frame++) {
        ULONG64 base = 0, established = 0;
        PVOID handler_data = nullptr;
        PVOID entry = RtlLookupFunctionEntry(context->Rip, &base, nullptr);
        if (!entry)
            break;
        RtlVirtualUnwind(0, base, context->Rip, entry, context, &handler_data, &established, nullptr);
    }
}

DECLSPEC_EXPORT DECLSPEC_NORETURN void __cdecl __report_gsfailure(ULONG_PTR cookie)
{
    ULONG_PTR address = (ULONG_PTR)__builtin_return_address(0);
    if (IsProcessorFeaturePresent(PF_FASTFAIL_AVAILABLE))
        fast_fail();
    capture_previous_context(&g_context);
    g_context.Rip = address;
    g_context.Rsp = (ULONG_PTR)__builtin_frame_address(0) + 2 * sizeof(void *);
    g_context.Rcx = cookie;
    fill_record(address);
    raise_security_failure();
}

#else

/* what the entry in assembly saved, last pushed first, and the return address after it. */
typedef struct GS_REGISTERS {
    ULONG gs, fs, es, ds, cs, ss, ebp, ecx, edx, ebx, esi, edi, eflags, eip;
} GS_REGISTERS;

DECLSPEC_NORETURN void MsvcrtReportGsFailure(const GS_REGISTERS *regs)
{
    if (IsProcessorFeaturePresent(PF_FASTFAIL_AVAILABLE))
        fast_fail();
    g_context.ContextFlags = CONTEXT_CONTROL;
    g_context.SegGs = regs->gs;
    g_context.SegFs = regs->fs;
    g_context.SegEs = regs->es;
    g_context.SegDs = regs->ds;
    g_context.SegCs = regs->cs;
    g_context.SegSs = regs->ss;
    g_context.Ebp = regs->ebp;
    g_context.Ecx = regs->ecx;
    g_context.Edx = regs->edx;
    g_context.Ebx = regs->ebx;
    g_context.Esi = regs->esi;
    g_context.Edi = regs->edi;
    g_context.EFlags = regs->eflags;
    g_context.Eip = regs->eip;
    g_context.Esp = (ULONG)(regs + 1);
    fill_record(regs->eip);
    raise_security_failure();
}

#endif

#endif
