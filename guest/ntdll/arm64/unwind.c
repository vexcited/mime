/*
 * arm64 structured exception handling: unwind data lookup, frame based dispatch, unwinding and the c
 * language handler, on top of the unwinder in unwind_arm64.c.
 *
 * the unwind data is Microsoft's published arm64 format, and dispatch follows the protocol compilers expect
 * from ntdll on x64: DISPATCHER_CONTEXT, the EXCEPTION_UNWINDING, EXCEPTION_EXIT_UNWIND and
 * EXCEPTION_TARGET_UNWIND flags, and collided unwinds. unlike x64, a frame's pc is the return address of its
 * call: an unwound context has CONTEXT_UNWOUND_TO_CALL set, and pc lookups, including the c handler's scope
 * tables, use the instruction before it.
 */
#include "../ntdll_private.h"
#include "unwind_arm64.h"

typedef IMAGE_ARM64_RUNTIME_FUNCTION_ENTRY RUNTIME_FUNCTION, *PRUNTIME_FUNCTION;

#define UNW_FLAG_NHANDLER 0
#define UNW_FLAG_EHANDLER 1
#define UNW_FLAG_UHANDLER 2

typedef enum _EXCEPTION_DISPOSITION {
    ExceptionContinueExecution,
    ExceptionContinueSearch,
    ExceptionNestedException,
    ExceptionCollidedUnwind,
} EXCEPTION_DISPOSITION;

struct _DISPATCHER_CONTEXT;
typedef EXCEPTION_DISPOSITION(NTAPI *PEXCEPTION_ROUTINE)(PEXCEPTION_RECORD record, PVOID establisher_frame,
                                                         PCONTEXT context, struct _DISPATCHER_CONTEXT *dispatcher);

typedef struct _DISPATCHER_CONTEXT {
    ULONG64 ControlPc;
    ULONG64 ImageBase;
    PRUNTIME_FUNCTION FunctionEntry;
    ULONG64 EstablisherFrame;
    ULONG64 TargetPc;
    PCONTEXT ContextRecord;
    PEXCEPTION_ROUTINE LanguageHandler;
    PVOID HandlerData;
    PVOID HistoryTable;
    ULONG ScopeIndex;
    BOOLEAN ControlPcIsUnwound;
    PUCHAR NonVolatileRegisters;
} DISPATCHER_CONTEXT, *PDISPATCHER_CONTEXT;

typedef struct _KNONVOLATILE_CONTEXT_POINTERS {
    PULONG64 X19, X20, X21, X22, X23, X24, X25, X26, X27, X28;
    PULONG64 Fp, Lr;
    PULONG64 D8, D9, D10, D11, D12, D13, D14, D15;
} KNONVOLATILE_CONTEXT_POINTERS, *PKNONVOLATILE_CONTEXT_POINTERS;

MIME_STATIC_ASSERT(sizeof(RUNTIME_FUNCTION) == sizeof(A64_FUNCTION), "runtime function");
MIME_STATIC_ASSERT(sizeof(KNONVOLATILE_CONTEXT_POINTERS) == sizeof(A64_POINTERS), "context pointers");
MIME_STATIC_ASSERT(offsetof(DISPATCHER_CONTEXT, ScopeIndex) == 0x48, "dispatcher context");
MIME_STATIC_ASSERT(offsetof(DISPATCHER_CONTEXT, NonVolatileRegisters) == 0x50, "dispatcher context");
MIME_STATIC_ASSERT(offsetof(CONTEXT, ContextFlags) == offsetof(A64_CONTEXT, flags), "context layout");
MIME_STATIC_ASSERT(offsetof(CONTEXT, Cpsr) == offsetof(A64_CONTEXT, cpsr), "context layout");
MIME_STATIC_ASSERT(offsetof(CONTEXT, X) == offsetof(A64_CONTEXT, x), "context layout");
MIME_STATIC_ASSERT(offsetof(CONTEXT, Sp) == offsetof(A64_CONTEXT, sp), "context layout");
MIME_STATIC_ASSERT(offsetof(CONTEXT, Pc) == offsetof(A64_CONTEXT, pc), "context layout");
MIME_STATIC_ASSERT(offsetof(CONTEXT, V) == offsetof(A64_CONTEXT, v), "context layout");

#define STATUS_UNWIND ((NTSTATUS)0xC0000027L)
#define STATUS_BAD_STACK ((NTSTATUS)0xC0000028L)
#define STATUS_BAD_FUNCTION_TABLE ((NTSTATUS)0xC00000FFL)
#define STATUS_UNWIND_CONSOLIDATE ((NTSTATUS)0x80000029L)
#ifndef STATUS_LONGJUMP
#define STATUS_LONGJUMP ((NTSTATUS)0x80000026L)
#endif
#define STATUS_INVALID_UNWIND_TARGET ((NTSTATUS)0xC0000029L)
#define EXCEPTION_NESTED_CALL 0x10
#define EXCEPTION_STACK_INVALID 0x8

/* ------------------------------------------------------------ function tables */

/* tables registered at run time with RtlAddFunctionTable, for generated code. */
typedef struct DYNAMIC_TABLE {
    LIST_ENTRY Links;
    PRUNTIME_FUNCTION Table;
    ULONG Count;
    ULONG64 Base;
    ULONG64 Begin;
    ULONG64 End;
    PRUNTIME_FUNCTION(NTAPI *Callback)(ULONG64, PVOID);
    PVOID Context;
} DYNAMIC_TABLE;

static LIST_ENTRY g_dynamic = {&g_dynamic, &g_dynamic};
static RTL_SRWLOCK g_dynamic_lock;

static PRUNTIME_FUNCTION search_table(ULONG64 base, PRUNTIME_FUNCTION table, ULONG count, ULONG rva)
{
    return (PRUNTIME_FUNCTION)a64_find_function(base, (const A64_FUNCTION *)table, count, rva);
}

NTDLL_EXPORT PVOID NTAPI RtlLookupFunctionEntry(ULONG64 pc, PULONG64 image_base, PVOID history)
{
    PLDR_DATA_TABLE_ENTRY entry;
    if (NT_SUCCESS(LdrFindEntryForAddress((PVOID)pc, &entry))) {
        ULONG size;
        PRUNTIME_FUNCTION table = LdrpImageDirectory(entry->DllBase, IMAGE_DIRECTORY_ENTRY_EXCEPTION, &size);
        *image_base = (ULONG64)entry->DllBase;
        if (!table)
            return nullptr;
        return search_table((ULONG64)entry->DllBase, table, size / sizeof(RUNTIME_FUNCTION),
                            (ULONG)(pc - (ULONG64)entry->DllBase));
    }
    *image_base = 0;
    PRUNTIME_FUNCTION found = nullptr;
    RtlAcquireSRWLockShared(&g_dynamic_lock);
    for (PLIST_ENTRY l = g_dynamic.Flink; l != &g_dynamic; l = l->Flink) {
        DYNAMIC_TABLE *t = CONTAINING_RECORD(l, DYNAMIC_TABLE, Links);
        if (pc >= t->Begin && pc < t->End) {
            if (t->Callback) {
                PRUNTIME_FUNCTION(NTAPI * callback)(ULONG64, PVOID) = t->Callback;
                PVOID context = t->Context;
                *image_base = t->Base;
                RtlReleaseSRWLockShared(&g_dynamic_lock);
                return callback(pc, context);
            }
            found = search_table(t->Base, t->Table, t->Count, (ULONG)(pc - t->Base));
            if (found) {
                *image_base = t->Base;
                break;
            }
        }
    }
    RtlReleaseSRWLockShared(&g_dynamic_lock);
    return found;
}

NTDLL_EXPORT BOOLEAN NTAPI RtlAddFunctionTable(PRUNTIME_FUNCTION table, ULONG count, ULONG64 base)
{
    DYNAMIC_TABLE *t = RtlAllocateHeap(NtCurrentPeb()->ProcessHeap, 0, sizeof(*t));
    if (!t)
        return FALSE;
    t->Table = table;
    t->Count = count;
    t->Base = base;
    t->Begin = base + (count ? table[0].BeginAddress : 0);
    t->End = base;
    if (count)
        t->End += table[count - 1].BeginAddress + a64_function_length(base, (const A64_FUNCTION *)&table[count - 1]);
    t->Callback = nullptr;
    t->Context = nullptr;
    RtlAcquireSRWLockExclusive(&g_dynamic_lock);
    InsertTailList(&g_dynamic, &t->Links);
    RtlReleaseSRWLockExclusive(&g_dynamic_lock);
    return TRUE;
}

NTDLL_EXPORT BOOLEAN NTAPI RtlInstallFunctionTableCallback(ULONG64 identifier, ULONG64 base, ULONG length,
    PRUNTIME_FUNCTION(NTAPI *callback)(ULONG64, PVOID), PVOID context, PCWSTR external_dll)
{
    if ((identifier & 3) != 3 || !callback || !length || base + length < base)
        return FALSE;
    DYNAMIC_TABLE *t = RtlAllocateHeap(NtCurrentPeb()->ProcessHeap, 0, sizeof(*t));
    if (!t)
        return FALSE;
    t->Table = (PRUNTIME_FUNCTION)identifier;
    t->Count = 0;
    t->Base = t->Begin = base;
    t->End = base + length;
    t->Callback = callback;
    t->Context = context;
    RtlAcquireSRWLockExclusive(&g_dynamic_lock);
    for (PLIST_ENTRY l = g_dynamic.Flink; l != &g_dynamic; l = l->Flink) {
        if (CONTAINING_RECORD(l, DYNAMIC_TABLE, Links)->Table == t->Table) {
            RtlReleaseSRWLockExclusive(&g_dynamic_lock);
            RtlFreeHeap(NtCurrentPeb()->ProcessHeap, 0, t);
            return FALSE;
        }
    }
    InsertTailList(&g_dynamic, &t->Links);
    RtlReleaseSRWLockExclusive(&g_dynamic_lock);
    return TRUE;
}

NTDLL_EXPORT BOOLEAN NTAPI RtlDeleteFunctionTable(PRUNTIME_FUNCTION table)
{
    DYNAMIC_TABLE *found = nullptr;
    RtlAcquireSRWLockExclusive(&g_dynamic_lock);
    for (PLIST_ENTRY l = g_dynamic.Flink; l != &g_dynamic; l = l->Flink) {
        DYNAMIC_TABLE *t = CONTAINING_RECORD(l, DYNAMIC_TABLE, Links);
        if (t->Table == table) {
            RemoveEntryList(&t->Links);
            found = t;
            break;
        }
    }
    RtlReleaseSRWLockExclusive(&g_dynamic_lock);
    if (found)
        RtlFreeHeap(NtCurrentPeb()->ProcessHeap, 0, found);
    return found != nullptr;
}

/*
 * a growable table covers a range from the start, and its caller appends sorted entries in place, then
 * publishes the new count.
 */
NTDLL_EXPORT NTSTATUS NTAPI RtlAddGrowableFunctionTable(PVOID *handle, PRUNTIME_FUNCTION table, ULONG count,
                                                        ULONG maximum, ULONG_PTR begin, ULONG_PTR end)
{
    if (!handle || count > maximum || end < begin)
        return STATUS_INVALID_PARAMETER;
    DYNAMIC_TABLE *t = RtlAllocateHeap(NtCurrentPeb()->ProcessHeap, 0, sizeof(*t));
    if (!t)
        return STATUS_NO_MEMORY;
    t->Table = table;
    t->Count = count;
    t->Base = t->Begin = begin;
    t->End = end;
    t->Callback = nullptr;
    t->Context = nullptr;
    RtlAcquireSRWLockExclusive(&g_dynamic_lock);
    InsertTailList(&g_dynamic, &t->Links);
    RtlReleaseSRWLockExclusive(&g_dynamic_lock);
    *handle = t;
    return STATUS_SUCCESS;
}

NTDLL_EXPORT void NTAPI RtlGrowFunctionTable(PVOID handle, ULONG count)
{
    DYNAMIC_TABLE *t = handle;
    RtlAcquireSRWLockExclusive(&g_dynamic_lock);
    t->Count = count;
    RtlReleaseSRWLockExclusive(&g_dynamic_lock);
}

NTDLL_EXPORT void NTAPI RtlDeleteGrowableFunctionTable(PVOID handle)
{
    DYNAMIC_TABLE *t = handle;
    RtlAcquireSRWLockExclusive(&g_dynamic_lock);
    RemoveEntryList(&t->Links);
    RtlReleaseSRWLockExclusive(&g_dynamic_lock);
    RtlFreeHeap(NtCurrentPeb()->ProcessHeap, 0, t);
}

/* ------------------------------------------------------------ virtual unwind */

/* an unwound frame is looked up by the call before its return address. */
static ULONG64 lookup_pc(const CONTEXT *c)
{
    return (c->ContextFlags & CONTEXT_UNWOUND_TO_CALL) ? c->Pc - 4 : c->Pc;
}

NTDLL_EXPORT PEXCEPTION_ROUTINE NTAPI RtlVirtualUnwind(ULONG type, ULONG64 base, ULONG64 pc, PRUNTIME_FUNCTION function,
                                                       PCONTEXT c, PVOID *handler_data, PULONG64 establisher_frame,
                                                       PKNONVOLATILE_CONTEXT_POINTERS pointers)
{
    A64_UNWIND_RESULT result;
    ULONG64 sp = c->Sp;
    if (!a64_virtual_unwind(base, pc, (const A64_FUNCTION *)function, (A64_CONTEXT *)c, (A64_POINTERS *)pointers,
                            &result, 0, ~0ull)) {
        /* unknown unwind data: continue as from a leaf rather than loop on the same frame. */
        a64_unwind_leaf((A64_CONTEXT *)c);
        *establisher_frame = sp;
        *handler_data = nullptr;
        return nullptr;
    }
    *establisher_frame = result.establisher;
    if ((type & (UNW_FLAG_EHANDLER | UNW_FLAG_UHANDLER)) && result.handler) {
        *handler_data = (PVOID)result.handler_data;
        return (PEXCEPTION_ROUTINE)(base + result.handler);
    }
    *handler_data = nullptr;
    return nullptr;
}

static BOOLEAN frame_in_stack(ULONG64 frame)
{
    PNT_TIB tib = &NtCurrentTeb()->NtTib;
    return frame >= (ULONG64)tib->StackLimit - 0x100000 && frame < (ULONG64)tib->StackBase && !(frame & 7);
}

/*
 * moves one frame up, by virtual unwind or as a leaf. returns false when the function's unwind data is invalid
 * or leads out of the stack, and the context must not be used then.
 */
static BOOLEAN unwind_frame(ULONG type, PCONTEXT c, PDISPATCHER_CONTEXT dc, PEXCEPTION_ROUTINE *handler)
{
    ULONG64 base = 0;
    ULONG64 pc = c->Pc;
    PRUNTIME_FUNCTION f = RtlLookupFunctionEntry(lookup_pc(c), &base, nullptr);
    dc->ControlPc = pc;
    dc->ImageBase = base;
    dc->FunctionEntry = f;
    dc->LanguageHandler = nullptr;
    dc->HandlerData = nullptr;
    dc->ControlPcIsUnwound = (c->ContextFlags & CONTEXT_UNWOUND_TO_CALL) != 0;
    *handler = nullptr;
    if (!f) {
        dc->EstablisherFrame = c->Sp;
        a64_unwind_leaf((A64_CONTEXT *)c);
        return TRUE;
    }
    A64_UNWIND_RESULT result;
    if (!a64_virtual_unwind(base, pc, (const A64_FUNCTION *)f, (A64_CONTEXT *)c, nullptr, &result, c->Sp,
                            (ULONG64)NtCurrentTeb()->NtTib.StackBase))
        return FALSE;
    dc->EstablisherFrame = result.establisher;
    if ((type & (UNW_FLAG_EHANDLER | UNW_FLAG_UHANDLER)) && result.handler) {
        *handler = (PEXCEPTION_ROUTINE)(base + result.handler);
        dc->LanguageHandler = *handler;
        dc->HandlerData = (PVOID)result.handler_data;
    }
    return TRUE;
}

/* the frame went up the stack, or the pc moved on in a leaf function. */
static BOOLEAN made_progress(const CONTEXT *before, const CONTEXT *after)
{
    return after->Sp > before->Sp || (after->Sp == before->Sp && after->Pc != before->Pc);
}

/* ------------------------------------------------------------ dispatch */

NTDLL_EXPORT BOOLEAN NTAPI RtlDispatchFrameException(PEXCEPTION_RECORD record, PCONTEXT context)
{
    CONTEXT c = *context;
    CONTEXT frame_context;
    DISPATCHER_CONTEXT dc;
    memset(&dc, 0, sizeof(dc));
    ULONG64 nested_frame = 0;
    for (ULONG depth = 0; depth < 4096; depth++) {
        if (!c.Pc || !frame_in_stack(c.Sp))
            break;
        frame_context = c;
        PEXCEPTION_ROUTINE handler;
        if (!unwind_frame(UNW_FLAG_EHANDLER, &c, &dc, &handler) || !made_progress(&frame_context, &c))
            break;
        if (!handler)
            continue;
        if (!frame_in_stack(dc.EstablisherFrame)) {
            record->ExceptionFlags |= EXCEPTION_STACK_INVALID;
            break;
        }
        dc.ContextRecord = &frame_context;
        dc.NonVolatileRegisters = (PUCHAR)&frame_context.X19;
        dc.TargetPc = 0;
        dc.ScopeIndex = 0;
        dc.HistoryTable = nullptr;
        EXCEPTION_DISPOSITION r = handler(record, (PVOID)dc.EstablisherFrame, context, &dc);
        if (nested_frame == dc.EstablisherFrame) {
            record->ExceptionFlags &= ~EXCEPTION_NESTED_CALL;
            nested_frame = 0;
        }
        switch (r) {
        case ExceptionContinueExecution:
            if (record->ExceptionFlags & EXCEPTION_NONCONTINUABLE) {
                EXCEPTION_RECORD nested = {};
                nested.ExceptionCode = STATUS_NONCONTINUABLE_EXCEPTION;
                nested.ExceptionFlags = EXCEPTION_NONCONTINUABLE;
                nested.ExceptionRecord = record;
                RtlRaiseException(&nested);
            }
            return TRUE;
        case ExceptionContinueSearch:
            break;
        case ExceptionNestedException:
            record->ExceptionFlags |= EXCEPTION_NESTED_CALL;
            if (dc.EstablisherFrame > nested_frame)
                nested_frame = dc.EstablisherFrame;
            break;
        default: {
            EXCEPTION_RECORD invalid = {};
            invalid.ExceptionCode = STATUS_INVALID_DISPOSITION;
            invalid.ExceptionFlags = EXCEPTION_NONCONTINUABLE;
            invalid.ExceptionRecord = record;
            RtlRaiseException(&invalid);
            break;
        }
        }
    }
    return FALSE;
}

/* raises with the caller's state, resuming after the call. */
NTDLL_EXPORT void NTAPI RtlRaiseException(PEXCEPTION_RECORD record)
{
    CONTEXT context;
    RtlCaptureContext(&context);
    /* step out of this function: its caller's registers and stack as they were at the call. */
    DISPATCHER_CONTEXT dc;
    PEXCEPTION_ROUTINE handler;
    CONTEXT here = context;
    if (!unwind_frame(UNW_FLAG_NHANDLER, &context, &dc, &handler) || !made_progress(&here, &context)) {
        context = here;
        context.Pc = (ULONG64)__builtin_return_address(0);
        context.Sp = (ULONG64)__builtin_frame_address(0) + 16;
    }
    record->ExceptionAddress = (PVOID)context.Pc;
    NTSTATUS status = NtRaiseException(record, &context, TRUE);
    RtlRaiseStatus(status);
}

/* ------------------------------------------------------------ unwinding */

PVOID RtlpCallConsolidate(PCONTEXT context, PVOID(NTAPI *callback)(PEXCEPTION_RECORD), PEXCEPTION_RECORD record);
void RtlpResumeContext(PCONTEXT context);

/* the jmp_buf of the arm64 c runtime, which longjmp unwinds to with STATUS_LONGJUMP. */
typedef struct _JUMP_BUFFER {
    ULONG64 Frame, Reserved;
    ULONG64 X[10];
    ULONG64 Fp, Lr, Sp;
    ULONG Fpcr, Fpsr;
    ULONG64 D[8];
} JUMP_BUFFER;
MIME_STATIC_ASSERT(offsetof(JUMP_BUFFER, D) == 128, "jump buffer");

/*
 * resumes a context an unwind reached. a consolidation first runs its callback, the catch block of a c++
 * exception, which returns where to continue; a longjmp takes the registers its jmp_buf saved.
 */
NTDLL_EXPORT void NTAPI RtlRestoreContext(PCONTEXT context, PEXCEPTION_RECORD record)
{
    if (record && record->ExceptionCode == STATUS_LONGJUMP && record->NumberParameters >= 1) {
        const JUMP_BUFFER *jmp = (const JUMP_BUFFER *)record->ExceptionInformation[0];
        for (int i = 0; i < 10; i++)
            context->X[19 + i] = jmp->X[i];
        context->Fp = jmp->Fp;
        context->Lr = jmp->Lr;
        context->Sp = jmp->Sp;
        context->Pc = jmp->Lr;
        for (int i = 0; i < 8; i++)
            context->V[8 + i].Low = jmp->D[i];
        context->Fpcr = jmp->Fpcr;
        context->Fpsr = jmp->Fpsr;
    } else if (record && record->ExceptionCode == STATUS_UNWIND_CONSOLIDATE && record->NumberParameters >= 1) {
        PVOID(NTAPI * callback)(PEXCEPTION_RECORD) = (PVOID(NTAPI *)(PEXCEPTION_RECORD))record->ExceptionInformation[0];
        context->Pc = (ULONG64)RtlpCallConsolidate(context, callback, record);
    }
    RtlpResumeContext(context);
}

NTDLL_EXPORT void NTAPI RtlUnwindEx(PVOID target_frame, PVOID target_ip, PEXCEPTION_RECORD record, PVOID value,
                                    PCONTEXT original, PVOID history)
{
    EXCEPTION_RECORD local;
    if (!record) {
        memset(&local, 0, sizeof(local));
        local.ExceptionCode = STATUS_UNWIND;
        local.ExceptionAddress = __builtin_return_address(0);
        record = &local;
    }
    record->ExceptionFlags |= EXCEPTION_UNWINDING;
    if (!target_frame)
        record->ExceptionFlags |= EXCEPTION_EXIT_UNWIND;

    /* start from this function's caller. */
    CONTEXT c;
    DISPATCHER_CONTEXT dc;
    PEXCEPTION_ROUTINE handler;
    memset(&dc, 0, sizeof(dc));
    RtlCaptureContext(&c);
    if (!unwind_frame(UNW_FLAG_NHANDLER, &c, &dc, &handler))
        RtlRaiseStatus(STATUS_BAD_FUNCTION_TABLE);

    for (ULONG depth = 0; depth < 4096; depth++) {
        CONTEXT next = c;
        if (!unwind_frame(UNW_FLAG_UHANDLER, &next, &dc, &handler) || !made_progress(&c, &next)) {
            /* an unwind that exits the thread is over when the stack is. */
            if (!target_frame)
                break;
            RtlRaiseStatus(STATUS_BAD_FUNCTION_TABLE);
        }
        if (!frame_in_stack(dc.EstablisherFrame) || (target_frame && dc.EstablisherFrame > (ULONG64)target_frame)) {
            DbgPrint("RtlUnwindEx: frame %p is not on the way to %p\n", (PVOID)dc.EstablisherFrame, target_frame);
            RtlRaiseStatus(STATUS_INVALID_UNWIND_TARGET);
        }
        BOOLEAN target = target_frame && dc.EstablisherFrame == (ULONG64)target_frame;
        if (target)
            record->ExceptionFlags |= EXCEPTION_TARGET_UNWIND;
        if (handler) {
            dc.ContextRecord = &c;
            dc.NonVolatileRegisters = (PUCHAR)&c.X19;
            dc.TargetPc = (ULONG64)target_ip;
            dc.HistoryTable = history;
            dc.ScopeIndex = 0;
            EXCEPTION_DISPOSITION r = handler(record, (PVOID)dc.EstablisherFrame, &c, &dc);
            record->ExceptionFlags &= ~(EXCEPTION_COLLIDED_UNWIND | EXCEPTION_TARGET_UNWIND);
            if (r == ExceptionCollidedUnwind) {
                /* another unwind went through this frame: continue from where it stopped. */
                record->ExceptionFlags |= EXCEPTION_COLLIDED_UNWIND;
            } else if (r != ExceptionContinueSearch) {
                RtlRaiseStatus(STATUS_INVALID_DISPOSITION);
            }
        }
        if (target)
            break;
        c = next;
        if (!target_frame && (!c.Pc || !frame_in_stack(c.Sp)))
            break;
    }
    c.X0 = (ULONG64)value;
    c.Pc = (ULONG64)target_ip;
    RtlRestoreContext(&c, record);
}

NTDLL_EXPORT void NTAPI RtlUnwind(PVOID target_frame, PVOID target_ip, PEXCEPTION_RECORD record, PVOID value)
{
    CONTEXT context;
    RtlUnwindEx(target_frame, target_ip, record, value, &context, nullptr);
}

/* ------------------------------------------------------------ c language handler */

typedef struct _SCOPE_TABLE {
    ULONG Count;
    struct {
        ULONG BeginAddress;
        ULONG EndAddress;
        ULONG HandlerAddress;
        ULONG JumpTarget;
    } ScopeRecord[1];
} SCOPE_TABLE;

typedef LONG(NTAPI *PEXCEPTION_FILTER)(PEXCEPTION_POINTERS pointers, PVOID frame);
typedef void(NTAPI *PTERMINATION_HANDLER)(BOOLEAN abnormal, PVOID frame);

#define EXCEPTION_EXECUTE_HANDLER 1
#define EXCEPTION_CONTINUE_SEARCH 0

NTDLL_EXPORT EXCEPTION_DISPOSITION NTAPI __C_specific_handler(PEXCEPTION_RECORD record, PVOID frame, PCONTEXT context,
                                                              PDISPATCHER_CONTEXT dc)
{
    const SCOPE_TABLE *scopes = dc->HandlerData;
    /* the scopes cover the calls, and the pc of an unwound frame is the instruction after one. */
    ULONG64 pc = dc->ControlPc - dc->ImageBase - (dc->ControlPcIsUnwound ? 4 : 0);
    if (!(record->ExceptionFlags & EXCEPTION_UNWIND)) {
        for (ULONG i = dc->ScopeIndex; i < scopes->Count; i++) {
            if (pc < scopes->ScopeRecord[i].BeginAddress || pc >= scopes->ScopeRecord[i].EndAddress)
                continue;
            if (!scopes->ScopeRecord[i].JumpTarget)
                continue;
            LONG filter;
            if (scopes->ScopeRecord[i].HandlerAddress == EXCEPTION_EXECUTE_HANDLER) {
                filter = EXCEPTION_EXECUTE_HANDLER;
            } else {
                EXCEPTION_POINTERS pointers = {record, context};
                filter = ((PEXCEPTION_FILTER)(dc->ImageBase + scopes->ScopeRecord[i].HandlerAddress))(&pointers, frame);
            }
            if (filter < 0)
                return ExceptionContinueExecution;
            if (filter > 0) {
                /* the handler goes on in the frame with the exception code in x0, which GetExceptionCode reads. */
                RtlUnwindEx(frame, (PVOID)(dc->ImageBase + scopes->ScopeRecord[i].JumpTarget), record,
                            (PVOID)(ULONG_PTR)(ULONG)record->ExceptionCode, dc->ContextRecord, dc->HistoryTable);
            }
        }
        return ExceptionContinueSearch;
    }
    ULONG64 target = dc->TargetPc - dc->ImageBase;
    for (ULONG i = dc->ScopeIndex; i < scopes->Count; i++) {
        if (pc < scopes->ScopeRecord[i].BeginAddress || pc >= scopes->ScopeRecord[i].EndAddress)
            continue;
        if ((record->ExceptionFlags & EXCEPTION_TARGET_UNWIND) && target >= scopes->ScopeRecord[i].BeginAddress &&
            target < scopes->ScopeRecord[i].EndAddress)
            break;
        if (scopes->ScopeRecord[i].JumpTarget) {
            /* an __except block of the target frame: the unwind stops at it. */
            if ((record->ExceptionFlags & EXCEPTION_TARGET_UNWIND) && target == scopes->ScopeRecord[i].JumpTarget)
                break;
            continue;
        }
        dc->ScopeIndex = i + 1;
        ((PTERMINATION_HANDLER)(dc->ImageBase + scopes->ScopeRecord[i].HandlerAddress))(TRUE, frame);
    }
    return ExceptionContinueSearch;
}
