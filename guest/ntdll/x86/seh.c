/*
 * x86 frame based exceptions: the handlers on the fs:0 list, newest first,
 * called during dispatch and unwind. each call is wrapped in an ntdll
 * registration that marks exceptions raised in a handler as nested and
 * overlapping unwinds as collided.
 */
#include "../ntdll_private.h"

typedef enum _EXCEPTION_DISPOSITION {
    ExceptionContinueExecution,
    ExceptionContinueSearch,
    ExceptionNestedException,
    ExceptionCollidedUnwind,
} EXCEPTION_DISPOSITION;

typedef EXCEPTION_DISPOSITION(__cdecl *PEXCEPTION_ROUTINE)(PEXCEPTION_RECORD record, PVOID frame, PCONTEXT context,
                                                          PVOID dispatcher);

typedef struct _EXCEPTION_REGISTRATION_RECORD {
    struct _EXCEPTION_REGISTRATION_RECORD *Next;
    PEXCEPTION_ROUTINE Handler;
} EXCEPTION_REGISTRATION_RECORD;

/* wraps a handler call, followed by the frame it was called for. */
typedef struct NESTED_REGISTRATION {
    EXCEPTION_REGISTRATION_RECORD Record;
    EXCEPTION_REGISTRATION_RECORD *Frame;
} NESTED_REGISTRATION;

#define END_OF_LIST ((EXCEPTION_REGISTRATION_RECORD *)~0u)
#define EXCEPTION_UNWIND_FLAGS (EXCEPTION_UNWINDING | EXCEPTION_EXIT_UNWIND)
#define EXCEPTION_NESTED_CALL 0x10
#define EXCEPTION_STACK_INVALID 0x8
#define STATUS_UNWIND ((NTSTATUS)0xC0000027L)
#define STATUS_INVALID_UNWIND_TARGET ((NTSTATUS)0xC0000029L)
#define STATUS_BAD_STACK ((NTSTATUS)0xC0000028L)

EXCEPTION_DISPOSITION __cdecl RtlpExecuteHandlerForException(PEXCEPTION_RECORD record, PVOID frame, PCONTEXT context,
                                                             PVOID dispatcher, PEXCEPTION_ROUTINE handler);
EXCEPTION_DISPOSITION __cdecl RtlpExecuteHandlerForUnwind(PEXCEPTION_RECORD record, PVOID frame, PCONTEXT context,
                                                          PVOID dispatcher, PEXCEPTION_ROUTINE handler);

EXCEPTION_DISPOSITION __cdecl RtlpNestedExceptionHandler(PEXCEPTION_RECORD record, PVOID frame, PCONTEXT context,
                                                         PVOID dispatcher)
{
    if (record->ExceptionFlags & EXCEPTION_UNWIND_FLAGS)
        return ExceptionContinueSearch;
    *(EXCEPTION_REGISTRATION_RECORD **)dispatcher = ((NESTED_REGISTRATION *)frame)->Frame;
    return ExceptionNestedException;
}

EXCEPTION_DISPOSITION __cdecl RtlpCollidedUnwindHandler(PEXCEPTION_RECORD record, PVOID frame, PCONTEXT context,
                                                        PVOID dispatcher)
{
    if (!(record->ExceptionFlags & EXCEPTION_UNWIND_FLAGS))
        return ExceptionContinueSearch;
    *(EXCEPTION_REGISTRATION_RECORD **)dispatcher = ((NESTED_REGISTRATION *)frame)->Frame;
    return ExceptionCollidedUnwind;
}

/* like windows, a registration must lie in the thread's stack before it is called. */
static BOOLEAN in_stack(EXCEPTION_REGISTRATION_RECORD *frame)
{
    TEB *teb = NtCurrentTeb();
    ULONG_PTR low = (ULONG_PTR)(teb->DeallocationStack ? teb->DeallocationStack : teb->NtTib.StackLimit);
    ULONG_PTR high = (ULONG_PTR)teb->NtTib.StackBase;
    return !((ULONG_PTR)frame & 3) && (ULONG_PTR)frame >= low && (ULONG_PTR)frame + sizeof(*frame) <= high;
}

BOOLEAN NTAPI RtlDispatchFrameException(PEXCEPTION_RECORD record, PCONTEXT context)
{
    EXCEPTION_REGISTRATION_RECORD *frame = NtCurrentTeb()->NtTib.ExceptionList, *nested = nullptr;
    while (frame && frame != END_OF_LIST) {
        if (!in_stack(frame)) {
            record->ExceptionFlags |= EXCEPTION_STACK_INVALID;
            return FALSE;
        }
        EXCEPTION_REGISTRATION_RECORD *dispatcher = nullptr;
        EXCEPTION_DISPOSITION disposition =
            RtlpExecuteHandlerForException(record, frame, context, &dispatcher, frame->Handler);
        if (frame == nested) {
            record->ExceptionFlags &= ~EXCEPTION_NESTED_CALL;
            nested = nullptr;
        }
        switch (disposition) {
        case ExceptionContinueExecution:
            if (!(record->ExceptionFlags & EXCEPTION_NONCONTINUABLE))
                return TRUE;
            {
                EXCEPTION_RECORD noncontinuable = {};
                noncontinuable.ExceptionCode = STATUS_NONCONTINUABLE_EXCEPTION;
                noncontinuable.ExceptionFlags = EXCEPTION_NONCONTINUABLE;
                noncontinuable.ExceptionRecord = record;
                RtlRaiseException(&noncontinuable);
            }
            break;
        case ExceptionContinueSearch:
            break;
        case ExceptionNestedException:
            /* raised from a handler: frames up to the one it was called for are skipped as nested. */
            record->ExceptionFlags |= EXCEPTION_NESTED_CALL;
            if (dispatcher > nested)
                nested = dispatcher;
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
        frame = frame->Next;
    }
    return FALSE;
}

/*
 * calls the handlers of frames newer than the target with the unwinding
 * flags, removing each from the list, then returns. the caller jumps to
 * the target frame's code itself. without a target the whole list is
 * unwound. windows returns through the context it captured with the value in eax, and code written in assembly
 * reads it there, as LuaJIT's lj_vm_rtlunwind does.
 */
PVOID NTAPI RtlpUnwind(PVOID target_frame, PVOID target_ip, PEXCEPTION_RECORD record, PVOID value)
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
    CONTEXT context;
    RtlCaptureContext(&context);
    context.Eip = (ULONG)__builtin_return_address(0);
    context.Esp = (ULONG)__builtin_frame_address(0) + 8 + 4 * sizeof(PVOID);
    context.Eax = (ULONG)value;
    NT_TIB *tib = &NtCurrentTeb()->NtTib;
    EXCEPTION_REGISTRATION_RECORD *frame = tib->ExceptionList;
    while (frame && frame != END_OF_LIST && frame != target_frame) {
        if (target_frame && (ULONG_PTR)target_frame < (ULONG_PTR)frame)
            RtlRaiseStatus(STATUS_INVALID_UNWIND_TARGET);
        if (!in_stack(frame))
            RtlRaiseStatus(STATUS_BAD_STACK);
        EXCEPTION_REGISTRATION_RECORD *dispatcher = nullptr;
        EXCEPTION_DISPOSITION disposition =
            RtlpExecuteHandlerForUnwind(record, frame, &context, &dispatcher, frame->Handler);
        if (disposition == ExceptionCollidedUnwind)
            frame = dispatcher;
        else if (disposition != ExceptionContinueSearch)
            RtlRaiseStatus(STATUS_INVALID_DISPOSITION);
        frame = frame->Next;
        tib->ExceptionList = frame;
    }
    return value;
}

/* a jump, so that the caller of RtlUnwind is the caller of the code above. */
__asm__(".text\n"
        ".globl _RtlUnwind@16\n"
        "_RtlUnwind@16:\n"
        "    jmp _RtlpUnwind@16\n");

NTDLL_EXPORT void NTAPI RtlUnwindEx(PVOID target_frame, PVOID target_ip, PEXCEPTION_RECORD record, PVOID value,
                                    PCONTEXT context, PVOID history)
{
    RtlUnwind(target_frame, target_ip, record, value);
}
