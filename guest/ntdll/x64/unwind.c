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

#define UNW_FLAG_NHANDLER 0
#define UNW_FLAG_EHANDLER 1
#define UNW_FLAG_UHANDLER 2
#define UNW_FLAG_CHAININFO 4

enum {
    UWOP_PUSH_NONVOL = 0,
    UWOP_ALLOC_LARGE = 1,
    UWOP_ALLOC_SMALL = 2,
    UWOP_SET_FPREG = 3,
    UWOP_SAVE_NONVOL = 4,
    UWOP_SAVE_NONVOL_FAR = 5,
    UWOP_EPILOG = 6,
    UWOP_SPARE_CODE = 7,
    UWOP_SAVE_XMM128 = 8,
    UWOP_SAVE_XMM128_FAR = 9,
    UWOP_PUSH_MACHFRAME = 10,
};

typedef struct _RUNTIME_FUNCTION {
    ULONG BeginAddress;
    ULONG EndAddress;
    ULONG UnwindData;
} RUNTIME_FUNCTION, *PRUNTIME_FUNCTION;

typedef union _UNWIND_CODE {
    struct {
        UCHAR CodeOffset;
        UCHAR UnwindOp : 4;
        UCHAR OpInfo : 4;
    };
    USHORT FrameOffset;
} UNWIND_CODE;

typedef struct _UNWIND_INFO {
    UCHAR Version : 3;
    UCHAR Flags : 5;
    UCHAR SizeOfProlog;
    UCHAR CountOfCodes;
    UCHAR FrameRegister : 4;
    UCHAR FrameOffset : 4;
    UNWIND_CODE UnwindCode[1];
} UNWIND_INFO;

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
    ULONG64 TargetIp;
    PCONTEXT ContextRecord;
    PEXCEPTION_ROUTINE LanguageHandler;
    PVOID HandlerData;
    PVOID HistoryTable;
    ULONG ScopeIndex;
    ULONG Fill0;
} DISPATCHER_CONTEXT, *PDISPATCHER_CONTEXT;

typedef struct _KNONVOLATILE_CONTEXT_POINTERS {
    M128A *FloatingContext[16];
    ULONG64 *IntegerContext[16];
} KNONVOLATILE_CONTEXT_POINTERS, *PKNONVOLATILE_CONTEXT_POINTERS;

#define STATUS_UNWIND ((NTSTATUS)0xC0000027L)
#define STATUS_INVALID_DISPOSITION ((NTSTATUS)0xC0000026L)
#define STATUS_NONCONTINUABLE_EXCEPTION ((NTSTATUS)0xC0000025L)
#define STATUS_UNWIND_CONSOLIDATE ((NTSTATUS)0x80000029L)
#ifndef STATUS_LONGJUMP
#define STATUS_LONGJUMP ((NTSTATUS)0x80000026L)
#endif
#define STATUS_INVALID_UNWIND_TARGET ((NTSTATUS)0xC0000029L)
#define EXCEPTION_NESTED_CALL 0x10
#define EXCEPTION_STACK_INVALID 0x8

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

/* an entry whose unwind data has the low bit set stands for the entry at that offset, which is the one that describes the code. */
#define RUNTIME_FUNCTION_INDIRECT 1

static PRUNTIME_FUNCTION search_table(PRUNTIME_FUNCTION table, ULONG count, ULONG rva, ULONG64 base)
{
    ULONG lo = 0, hi = count;
    while (lo < hi) {
        ULONG mid = (lo + hi) / 2;
        if (rva < table[mid].BeginAddress)
            hi = mid;
        else if (rva >= table[mid].EndAddress)
            lo = mid + 1;
        else if (table[mid].UnwindData & RUNTIME_FUNCTION_INDIRECT)
            return (PRUNTIME_FUNCTION)(base + table[mid].UnwindData - RUNTIME_FUNCTION_INDIRECT);
        else
            return &table[mid];
    }
    return nullptr;
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

/* ------------------------------------------------------------ virtual unwind */

static ULONG64 *gpr(PCONTEXT c, ULONG reg)
{
    return &c->Rax + reg;
}

static M128A *xmm(PCONTEXT c, ULONG reg)
{
    return &c->Xmm0 + reg;
}

static ULONG code_slots(const UNWIND_CODE *code)
{
    switch (code->UnwindOp) {
    case UWOP_ALLOC_LARGE:
        return code->OpInfo ? 3 : 2;
    case UWOP_SAVE_NONVOL:
    case UWOP_SAVE_XMM128:
    case UWOP_EPILOG:
        return 2;
    case UWOP_SAVE_NONVOL_FAR:
    case UWOP_SAVE_XMM128_FAR:
        return 3;
    default:
        return 1;
    }
}

/*
 * when pc sits in an epilog, the rest of the epilog is emulated instead of
 * the unwind codes: an optional stack release, pops of nonvolatile
 * registers, then ret or a jump out of the function.
 */
static BOOLEAN in_epilog(ULONG64 pc, ULONG64 base, PRUNTIME_FUNCTION f, const UNWIND_INFO *info)
{
    const UCHAR *p = (const UCHAR *)pc;
    /* add rsp, imm8 or imm32, or lea rsp, [frame + disp]. */
    if ((p[0] & 0xF8) == 0x48) {
        if (p[1] == 0x83 && p[2] == 0xC4)
            p += 4;
        else if (p[1] == 0x81 && p[2] == 0xC4)
            p += 7;
        else if (p[1] == 0x8D && (p[2] & 0x38) == 0x20) {
            UCHAR mod = p[2] >> 6;
            if (info->FrameRegister == 0 || (ULONG)((p[2] & 7) | ((p[0] & 1) << 3)) != info->FrameRegister)
                return FALSE;
            p += mod == 1 ? 4 : mod == 2 ? 7 : 3;
        }
    }
    for (;;) {
        if ((p[0] & 0xF8) == 0x58) {
            p++;
        } else if ((p[0] & 0xF0) == 0x40 && (p[1] & 0xF8) == 0x58) {
            p += 2;
        } else {
            break;
        }
    }
    if (p[0] == 0xC3 || (p[0] == 0xF3 && p[1] == 0xC3) || p[0] == 0xC2)
        return TRUE;
    /* a tail jump leaving the function. */
    if (p[0] == 0xE9) {
        LONG rel;
        memcpy(&rel, p + 1, 4);
        ULONG64 target = (ULONG64)p + 5 + (LONG64)rel;
        return target < base + f->BeginAddress || target >= base + f->EndAddress;
    }
    if (p[0] == 0xFF && (p[1] & 0x38) == 0x20)
        return TRUE;
    if ((p[0] & 0xF0) == 0x40 && p[1] == 0xFF && (p[2] & 0x38) == 0x20)
        return TRUE;
    return FALSE;
}

static void run_epilog(PCONTEXT c, const UNWIND_INFO *info, PKNONVOLATILE_CONTEXT_POINTERS pointers)
{
    const UCHAR *p = (const UCHAR *)c->Rip;
    if ((p[0] & 0xF8) == 0x48) {
        if (p[1] == 0x83 && p[2] == 0xC4) {
            c->Rsp += (LONG64)(signed char)p[3];
            p += 4;
        } else if (p[1] == 0x81 && p[2] == 0xC4) {
            LONG v;
            memcpy(&v, p + 3, 4);
            c->Rsp += (LONG64)v;
            p += 7;
        } else if (p[1] == 0x8D) {
            UCHAR mod = p[2] >> 6;
            ULONG reg = (p[2] & 7) | ((p[0] & 1) << 3);
            LONG disp = 0;
            if (mod == 1)
                disp = (signed char)p[3];
            else if (mod == 2)
                memcpy(&disp, p + 3, 4);
            c->Rsp = *gpr(c, reg) + (LONG64)disp;
            p += mod == 1 ? 4 : mod == 2 ? 7 : 3;
        }
    }
    for (;;) {
        ULONG reg;
        if ((p[0] & 0xF8) == 0x58) {
            reg = p[0] & 7;
            p++;
        } else if ((p[0] & 0xF0) == 0x40 && (p[1] & 0xF8) == 0x58) {
            reg = (p[1] & 7) | ((p[0] & 1) << 3);
            p += 2;
        } else {
            break;
        }
        *gpr(c, reg) = *(ULONG64 *)c->Rsp;
        if (pointers)
            pointers->IntegerContext[reg] = (ULONG64 *)c->Rsp;
        c->Rsp += 8;
    }
    /* ret or jump: the return address is on the stack either way. */
    c->Rip = *(ULONG64 *)c->Rsp;
    c->Rsp += 8;
    if (p[0] == 0xC2) {
        USHORT n;
        memcpy(&n, p + 1, 2);
        c->Rsp += n;
    }
    (void)info;
}

NTDLL_EXPORT PEXCEPTION_ROUTINE NTAPI RtlVirtualUnwind(ULONG type, ULONG64 base, ULONG64 pc, PRUNTIME_FUNCTION function,
                                                       PCONTEXT c, PVOID *handler_data, PULONG64 establisher_frame,
                                                       PKNONVOLATILE_CONTEXT_POINTERS pointers)
{
    const UNWIND_INFO *info = (const UNWIND_INFO *)(base + function->UnwindData);
    ULONG64 offset = pc - base - function->BeginAddress;
    ULONG64 frame_base = 0;
    BOOLEAN have_frame = FALSE;

    /* the establisher frame is the stack pointer at entry, or the frame register. */
    if (info->FrameRegister) {
        /* before the frame register is set, the stack pointer still holds it. */
        BOOLEAN set = TRUE;
        for (ULONG i = 0; i < info->CountOfCodes; i += code_slots(&info->UnwindCode[i]))
            if (info->UnwindCode[i].UnwindOp == UWOP_SET_FPREG && offset < info->UnwindCode[i].CodeOffset)
                set = FALSE;
        if (set) {
            frame_base = *gpr(c, info->FrameRegister) - 16ull * info->FrameOffset;
            have_frame = TRUE;
        }
    }
    *establisher_frame = have_frame ? frame_base : c->Rsp;
    /*
     * mov saves and the frame register are found from the frame base, the
     * stack pointer right after the prolog: the body may have moved rsp
     * since, and the codes restore the frame register before reaching the
     * one that set it.
     */
    ULONG64 frame = *establisher_frame;

    if (offset >= info->SizeOfProlog && in_epilog(pc, base, function, info)) {
        run_epilog(c, info, pointers);
        *handler_data = nullptr;
        return nullptr;
    }

    for (;;) {
        for (ULONG i = 0; i < info->CountOfCodes; i += code_slots(&info->UnwindCode[i])) {
            const UNWIND_CODE *code = &info->UnwindCode[i];
            /* skip codes for prolog instructions that have not run yet. */
            if (offset < code->CodeOffset && offset < info->SizeOfProlog)
                continue;
            ULONG op = code->UnwindOp, opinfo = code->OpInfo;
            switch (op) {
            case UWOP_PUSH_NONVOL:
                *gpr(c, opinfo) = *(ULONG64 *)c->Rsp;
                if (pointers)
                    pointers->IntegerContext[opinfo] = (ULONG64 *)c->Rsp;
                c->Rsp += 8;
                break;
            case UWOP_ALLOC_LARGE:
                if (!opinfo)
                    c->Rsp += 8ull * code[1].FrameOffset;
                else
                    c->Rsp += (ULONG64)code[1].FrameOffset | ((ULONG64)code[2].FrameOffset << 16);
                break;
            case UWOP_ALLOC_SMALL:
                c->Rsp += 8ull * opinfo + 8;
                break;
            case UWOP_SET_FPREG:
                c->Rsp = frame;
                break;
            case UWOP_SAVE_NONVOL:
                *gpr(c, opinfo) = *(ULONG64 *)(frame + 8ull * code[1].FrameOffset);
                if (pointers)
                    pointers->IntegerContext[opinfo] = (ULONG64 *)(frame + 8ull * code[1].FrameOffset);
                break;
            case UWOP_SAVE_NONVOL_FAR:
                *gpr(c, opinfo) = *(ULONG64 *)(frame + ((ULONG64)code[1].FrameOffset | ((ULONG64)code[2].FrameOffset << 16)));
                if (pointers)
                    pointers->IntegerContext[opinfo] = (ULONG64 *)(frame + ((ULONG64)code[1].FrameOffset | ((ULONG64)code[2].FrameOffset << 16)));
                break;
            case UWOP_SAVE_XMM128:
                memcpy(xmm(c, opinfo), (void *)(frame + 16ull * code[1].FrameOffset), 16);
                if (pointers)
                    pointers->FloatingContext[opinfo] = (M128A *)(frame + 16ull * code[1].FrameOffset);
                break;
            case UWOP_SAVE_XMM128_FAR:
                memcpy(xmm(c, opinfo),
                       (void *)(frame + ((ULONG64)code[1].FrameOffset | ((ULONG64)code[2].FrameOffset << 16))), 16);
                if (pointers)
                    pointers->FloatingContext[opinfo] = (M128A *)(frame + ((ULONG64)code[1].FrameOffset | ((ULONG64)code[2].FrameOffset << 16)));
                break;
            case UWOP_PUSH_MACHFRAME:
                if (opinfo)
                    c->Rsp += 8;
                c->Rip = *(ULONG64 *)c->Rsp;
                c->Rsp = *(ULONG64 *)(c->Rsp + 24);
                *handler_data = nullptr;
                return nullptr;
            default: /* epilog descriptors and spare codes have nothing to undo. */
                break;
            }
        }
        if (!(info->Flags & UNW_FLAG_CHAININFO))
            break;
        /* the chained entry follows the codes, aligned to an even count. */
        PRUNTIME_FUNCTION chained =
            (PRUNTIME_FUNCTION)&info->UnwindCode[(info->CountOfCodes + 1) & ~1];
        info = (const UNWIND_INFO *)(base + chained->UnwindData);
        offset = ~0ull; /* a chained parent's prolog ran completely. */
    }

    c->Rip = *(ULONG64 *)c->Rsp;
    c->Rsp += 8;

    info = (const UNWIND_INFO *)(base + function->UnwindData);
    while (info->Flags & UNW_FLAG_CHAININFO) {
        PRUNTIME_FUNCTION chained = (PRUNTIME_FUNCTION)&info->UnwindCode[(info->CountOfCodes + 1) & ~1];
        info = (const UNWIND_INFO *)(base + chained->UnwindData);
    }
    if (info->Flags & type & (UNW_FLAG_EHANDLER | UNW_FLAG_UHANDLER)) {
        const ULONG *handler = (const ULONG *)&info->UnwindCode[(info->CountOfCodes + 1) & ~1];
        *handler_data = (PVOID)(handler + 1);
        return (PEXCEPTION_ROUTINE)(base + *handler);
    }
    *handler_data = nullptr;
    return nullptr;
}

/* virtual unwind, or a leaf function returning straight away. */
static PEXCEPTION_ROUTINE unwind_frame(ULONG type, PCONTEXT c, PDISPATCHER_CONTEXT dc)
{
    ULONG64 base = 0;
    PRUNTIME_FUNCTION f = RtlLookupFunctionEntry(c->Rip, &base, nullptr);
    dc->ControlPc = c->Rip;
    dc->ImageBase = base;
    dc->FunctionEntry = f;
    dc->LanguageHandler = nullptr;
    dc->HandlerData = nullptr;
    if (!f) {
        dc->EstablisherFrame = c->Rsp;
        c->Rip = *(ULONG64 *)c->Rsp;
        c->Rsp += 8;
        return nullptr;
    }
    PVOID data = nullptr;
    PEXCEPTION_ROUTINE handler = RtlVirtualUnwind(type, base, c->Rip, f, c, &data, &dc->EstablisherFrame, nullptr);
    dc->LanguageHandler = handler;
    dc->HandlerData = data;
    return handler;
}

static BOOLEAN frame_in_stack(ULONG64 frame)
{
    PNT_TIB tib = &NtCurrentTeb()->NtTib;
    return frame >= (ULONG64)tib->StackLimit - 0x100000 && frame < (ULONG64)tib->StackBase && !(frame & 7);
}

/* ------------------------------------------------------------ dispatch */

NTDLL_EXPORT BOOLEAN NTAPI RtlDispatchFrameException(PEXCEPTION_RECORD record, PCONTEXT context)
{
    CONTEXT c = *context;
    DISPATCHER_CONTEXT dc;
    memset(&dc, 0, sizeof(dc));
    ULONG64 nested_frame = 0;
    for (ULONG depth = 0; depth < 4096; depth++) {
        if (!c.Rip || !frame_in_stack(c.Rsp))
            break;
        PEXCEPTION_ROUTINE handler = unwind_frame(UNW_FLAG_EHANDLER, &c, &dc);
        if (!handler)
            continue;
        if (!frame_in_stack(dc.EstablisherFrame)) {
            record->ExceptionFlags |= EXCEPTION_STACK_INVALID;
            break;
        }
        /* the context the dispatcher context holds is the caller's, the working context after this frame was unwound. */
        dc.ContextRecord = &c;
        dc.TargetIp = 0;
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
        case ExceptionCollidedUnwind:
            /*
             * the handler replaced the working context in the dispatcher context with the context of another
             * frame, as the runtime of the .NET Framework does for the stubs it puts between a fault and the code
             * that faulted. The walk goes on from that context, whose function has handlers of its own.
             */
            break;
        default: {
            DbgPrint("ntdll: the handler %p of frame %p returned %d for exception %08x\n", (PVOID)handler,
                     (PVOID)dc.EstablisherFrame, (int)r, (ULONG)record->ExceptionCode);
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

/* ------------------------------------------------------------ unwinding */

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

EXCEPTION_DISPOSITION NTAPI RtlpExecuteHandlerForUnwind(PEXCEPTION_RECORD record, PVOID frame, PCONTEXT context,
                                                        PDISPATCHER_CONTEXT dispatcher, PEXCEPTION_ROUTINE handler);

/*
 * the handler of RtlpExecuteHandlerForUnwind, reached by an exception that a handler called in an unwind raised.
 * the frames below the one under unwind are unwound already, so the search, and the unwind of the new exception,
 * go on from the context of that frame.
 */
EXCEPTION_DISPOSITION NTAPI RtlpUnwindHandler(PEXCEPTION_RECORD record, PVOID frame, PCONTEXT context,
                                              PDISPATCHER_CONTEXT dispatcher)
{
    PDISPATCHER_CONTEXT outer = *(PDISPATCHER_CONTEXT *)((PUCHAR)frame + 0x20);
    *dispatcher->ContextRecord = *outer->ContextRecord;
    return ExceptionCollidedUnwind;
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

    /* this function's caller frame. */
    CONTEXT c;
    RtlCaptureContext(&c);
    DISPATCHER_CONTEXT dc;
    memset(&dc, 0, sizeof(dc));
    unwind_frame(UNW_FLAG_NHANDLER, &c, &dc);

    for (ULONG depth = 0; depth < 4096; depth++) {
        CONTEXT next = c;
        PEXCEPTION_ROUTINE handler = unwind_frame(UNW_FLAG_UHANDLER, &next, &dc);
        if (!frame_in_stack(dc.EstablisherFrame) || (target_frame && dc.EstablisherFrame > (ULONG64)target_frame)) {
            DbgPrint("RtlUnwindEx: frame %p is not on the way to %p\n", (PVOID)dc.EstablisherFrame, target_frame);
            RtlRaiseStatus(STATUS_INVALID_UNWIND_TARGET);
        }
        BOOLEAN target = target_frame && dc.EstablisherFrame == (ULONG64)target_frame;
        if (target)
            record->ExceptionFlags |= EXCEPTION_TARGET_UNWIND;
        if (handler) {
            /* unlike the dispatch, the frame's own context, before it is unwound. */
            dc.ContextRecord = &c;
            dc.TargetIp = (ULONG64)target_ip;
            dc.HistoryTable = history;
            dc.ScopeIndex = 0;
            EXCEPTION_DISPOSITION r = RtlpExecuteHandlerForUnwind(record, (PVOID)dc.EstablisherFrame, &c, &dc, handler);
            record->ExceptionFlags &= ~(EXCEPTION_COLLIDED_UNWIND | EXCEPTION_TARGET_UNWIND);
            if (r == ExceptionCollidedUnwind) {
                /*
                 * the handler left the context of another frame in the dispatcher context, which is c. That frame
                 * is unwound from there, and its handlers are told of the collision.
                 */
                record->ExceptionFlags |= EXCEPTION_COLLIDED_UNWIND;
                continue;
            } else if (r != ExceptionContinueSearch) {
                RtlRaiseStatus(STATUS_INVALID_DISPOSITION);
            }
        }
        if (target)
            break;
        c = next;
        if (!target_frame && (!c.Rip || !frame_in_stack(c.Rsp)))
            break;
    }
    c.Rax = (ULONG64)value;
    c.Rip = (ULONG64)target_ip;
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
    ULONG64 pc = dc->ControlPc - dc->ImageBase;
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
                RtlUnwindEx(frame, (PVOID)(dc->ImageBase + scopes->ScopeRecord[i].JumpTarget), record,
                            (PVOID)(ULONG_PTR)(ULONG)record->ExceptionCode, dc->ContextRecord, dc->HistoryTable);
            }
        }
        return ExceptionContinueSearch;
    }
    ULONG64 target = dc->TargetIp - dc->ImageBase;
    for (ULONG i = dc->ScopeIndex; i < scopes->Count; i++) {
        if (pc < scopes->ScopeRecord[i].BeginAddress || pc >= scopes->ScopeRecord[i].EndAddress)
            continue;
        if ((record->ExceptionFlags & EXCEPTION_TARGET_UNWIND) && target >= scopes->ScopeRecord[i].BeginAddress &&
            target < scopes->ScopeRecord[i].EndAddress)
            break;
        if (scopes->ScopeRecord[i].JumpTarget) {
            /* the unwind stops at an __except block of the target frame. */
            if ((record->ExceptionFlags & EXCEPTION_TARGET_UNWIND) && target == scopes->ScopeRecord[i].JumpTarget)
                break;
            continue;
        }
        dc->ScopeIndex = i + 1;
        ((PTERMINATION_HANDLER)(dc->ImageBase + scopes->ScopeRecord[i].HandlerAddress))(TRUE, frame);
    }
    return ExceptionContinueSearch;
}
