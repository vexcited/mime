/*
 * x64 structured exception handling: unwind data lookup, the virtual
 * unwinder, frame based dispatch, unwinding and the c language handler.
 *
 * the unwind data is the public x64 UNWIND_INFO format. dispatch follows
 * what compiler generated handlers expect from ntdll: DISPATCHER_CONTEXT,
 * the EXCEPTION_UNWINDING, EXCEPTION_EXIT_UNWIND and EXCEPTION_TARGET_UNWIND
 * flags, and collided unwinds.
 */
#include "../ntdll_private.h"

#include <seh/x64seh.h>

/* ------------------------------------------------------------ function tables */

/* tables registered at run time with RtlAddFunctionTable for generated code. */
typedef struct DYNAMIC_TABLE {
    LIST_ENTRY Links;
    PRUNTIME_FUNCTION Table;
    ULONG Count;
    ULONG64 Base;
    ULONG64 Begin;
    ULONG64 End;
    PRUNTIME_FUNCTION (NTAPI *Callback)(ULONG64, PVOID);
    PVOID Context;
} DYNAMIC_TABLE;

static LIST_ENTRY g_dynamic = {&g_dynamic, &g_dynamic};
static RTL_SRWLOCK g_dynamic_lock;

NTDLL_EXPORT PVOID NTAPI RtlLookupFunctionEntry(ULONG64 pc, PULONG64 image_base, PVOID history)
{
    PLDR_DATA_TABLE_ENTRY entry;
    if (NT_SUCCESS(LdrFindEntryForAddress((PVOID)pc, &entry))) {
        ULONG size;
        PRUNTIME_FUNCTION table = LdrpImageDirectory(entry->DllBase, IMAGE_DIRECTORY_ENTRY_EXCEPTION, &size);
        *image_base = (ULONG64)entry->DllBase;
        if (!table)
            return nullptr;
        return search_table(table, size / sizeof(RUNTIME_FUNCTION), (ULONG)(pc - (ULONG64)entry->DllBase), (ULONG64)entry->DllBase);
    }
    PRUNTIME_FUNCTION found = nullptr;
    RtlAcquireSRWLockShared(&g_dynamic_lock);
    for (PLIST_ENTRY l = g_dynamic.Flink; l != &g_dynamic; l = l->Flink) {
        DYNAMIC_TABLE *t = CONTAINING_RECORD(l, DYNAMIC_TABLE, Links);
        if (pc >= t->Begin && pc < t->End) {
            if (t->Callback) {
                PRUNTIME_FUNCTION (NTAPI *callback)(ULONG64, PVOID) = t->Callback;
                PVOID context = t->Context;
                *image_base = t->Base;
                RtlReleaseSRWLockShared(&g_dynamic_lock);
                return callback(pc, context);
            }
            found = search_table(t->Table, t->Count, (ULONG)(pc - t->Base), t->Base);
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
    t->End = base + (count ? table[count - 1].EndAddress : 0);
    t->Callback = nullptr;
    t->Context = nullptr;
    RtlAcquireSRWLockExclusive(&g_dynamic_lock);
    InsertTailList(&g_dynamic, &t->Links);
    RtlReleaseSRWLockExclusive(&g_dynamic_lock);
    return TRUE;
}

NTDLL_EXPORT BOOLEAN NTAPI RtlInstallFunctionTableCallback(ULONG64 identifier, ULONG64 base, ULONG length,
    PRUNTIME_FUNCTION (NTAPI *callback)(ULONG64, PVOID), PVOID context, PCWSTR external_dll)
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
 * a growable table covers a range from the start, and its caller appends
 * sorted entries in place, then publishes the new count.
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

static BOOLEAN frame_in_stack(ULONG64 frame)
{
    PNT_TIB tib = &NtCurrentTeb()->NtTib;
    return frame >= (ULONG64)tib->StackLimit - 0x100000 && frame < (ULONG64)tib->StackBase && !(frame & 7);
}

NTDLL_EXPORT void NTAPI RtlRestoreContext(PCONTEXT context, PEXCEPTION_RECORD record);

#define SEH_EXPORT NTDLL_EXPORT
#include <seh/x64unwind.inc>

PVOID RtlpCallConsolidate(PCONTEXT context, PVOID(NTAPI *callback)(PEXCEPTION_RECORD), PEXCEPTION_RECORD record);

/* the x64 c runtime jmp_buf, which longjmp unwinds to with STATUS_LONGJUMP. */
typedef struct _JUMP_BUFFER {
    ULONG64 Frame, Rbx, Rsp, Rbp, Rsi, Rdi, R12, R13, R14, R15, Rip;
    ULONG MxCsr;
    USHORT FpCsr, Spare;
    M128A Xmm6, Xmm7, Xmm8, Xmm9, Xmm10, Xmm11, Xmm12, Xmm13, Xmm14, Xmm15;
} JUMP_BUFFER;

/*
 * a consolidation first runs its callback, the catch block of a c++
 * exception, which returns where to continue. a longjmp takes the
 * registers its jmp_buf saved.
 */
NTDLL_EXPORT void NTAPI RtlRestoreContext(PCONTEXT context, PEXCEPTION_RECORD record)
{
    if (record && record->ExceptionCode == STATUS_LONGJUMP && record->NumberParameters >= 1) {
        const JUMP_BUFFER *jmp = (const JUMP_BUFFER *)record->ExceptionInformation[0];
        context->Rbx = jmp->Rbx;
        context->Rsp = jmp->Rsp;
        context->Rbp = jmp->Rbp;
        context->Rsi = jmp->Rsi;
        context->Rdi = jmp->Rdi;
        context->R12 = jmp->R12;
        context->R13 = jmp->R13;
        context->R14 = jmp->R14;
        context->R15 = jmp->R15;
        context->Rip = jmp->Rip;
        memcpy(xmm(context, 6), &jmp->Xmm6, 10 * sizeof(M128A));
        context->MxCsr = jmp->MxCsr;
        context->FltSave.MxCsr = jmp->MxCsr;
        context->FltSave.ControlWord = jmp->FpCsr;
    } else if (record && record->ExceptionCode == STATUS_UNWIND_CONSOLIDATE && record->NumberParameters >= 1) {
        PVOID(NTAPI * callback)(PEXCEPTION_RECORD) = (PVOID(NTAPI *)(PEXCEPTION_RECORD))record->ExceptionInformation[0];
        context->Rip = (ULONG64)RtlpCallConsolidate(context, callback, record);
    }
    NtContinue(context, FALSE);
}

NTDLL_EXPORT void NTAPI RtlUnwind(PVOID target_frame, PVOID target_ip, PEXCEPTION_RECORD record, PVOID value)
{
    CONTEXT context;
    RtlUnwindEx(target_frame, target_ip, record, value, &context, nullptr);
}

extern const char KiUserCallbackDispatcherContinue[];

/*
 * the handler of KiUserCallbackDispatcher. by default what the callback leaves is fatal. with the process flag, the
 * callback is unwound and returns the code to the kernel, and an unwind that crosses it ends the callback there.
 */
EXCEPTION_DISPOSITION NTAPI KiUserCallbackDispatcherHandler(PEXCEPTION_RECORD record, PVOID frame, PCONTEXT context,
                                                            PDISPATCHER_CONTEXT dispatcher)
{
    if (!LdrpUserCallbackExceptions()) {
        LdrpLogFatalUserCallbackException(record, context);
        return ExceptionContinueExecution;
    }
    if (!(record->ExceptionFlags & EXCEPTION_UNWIND))
        RtlUnwindEx(frame, (PVOID)KiUserCallbackDispatcherContinue, record,
                    (PVOID)(ULONG_PTR)(ULONG)record->ExceptionCode, context, nullptr);
    if (!(record->ExceptionFlags & EXCEPTION_TARGET_UNWIND))
        RtlRaiseStatus(NtCallbackReturn(nullptr, 0, record->ExceptionCode));
    return ExceptionContinueSearch;
}

