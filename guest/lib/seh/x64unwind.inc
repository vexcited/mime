/*
 * the x64 unwinder ntdll and the kernel share: the virtual unwinder, frame based dispatch, unwinding and the c
 * language handler. the includer defines SEH_EXPORT and gives RtlLookupFunctionEntry, frame_in_stack,
 * RtlRestoreContext, RtlRaiseException, RtlRaiseStatus, RtlCaptureContext and RtlpExecuteHandlerForUnwind.
 */
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

/* the start of the function whose unwind data a chain of parts ends in. */
static ULONG primary_begin(ULONG64 base, PRUNTIME_FUNCTION f)
{
    const UNWIND_INFO *info = (const UNWIND_INFO *)(base + f->UnwindData);
    ULONG begin = f->BeginAddress;
    while (info->Flags & UNW_FLAG_CHAININFO) {
        PRUNTIME_FUNCTION chained = (PRUNTIME_FUNCTION)&info->UnwindCode[(info->CountOfCodes + 1) & ~1];
        begin = chained->BeginAddress;
        info = (const UNWIND_INFO *)(base + chained->UnwindData);
    }
    return begin;
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
    if (p[0] == 0xF2)
        p++;
    if (p[0] == 0xC3 || (p[0] == 0xF3 && p[1] == 0xC3) || p[0] == 0xC2)
        return TRUE;
    /*
     * a jump ends an epilog when it leaves the function, or goes back to its start. A function split in parts
     * that chain their unwind data to one primary entry jumps between them with its frame still built, and what
     * follows such a jump is not an epilog.
     */
    if (p[0] == 0xE9 || p[0] == 0xEB) {
        LONG rel;
        ULONG64 target;
        if (p[0] == 0xE9) {
            memcpy(&rel, p + 1, 4);
            target = (ULONG64)p + 5 + (LONG64)rel;
        } else {
            target = (ULONG64)p + 2 + (LONG64)(signed char)p[1];
        }
        ULONG64 rva = target - base;
        if (rva >= f->BeginAddress && rva < f->EndAddress)
            return rva == f->BeginAddress && !(info->Flags & UNW_FLAG_CHAININFO);
        ULONG64 other_base;
        PRUNTIME_FUNCTION other = RtlLookupFunctionEntry(target, &other_base, nullptr);
        if (!other || other_base != base)
            return TRUE;
        ULONG primary = primary_begin(base, f);
        return primary_begin(base, other) != primary || rva == primary;
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

SEH_EXPORT PEXCEPTION_ROUTINE NTAPI RtlVirtualUnwind(ULONG type, ULONG64 base, ULONG64 pc, PRUNTIME_FUNCTION function,
                                                       PCONTEXT c, PVOID *handler_data, PULONG64 establisher_frame,
                                                       PKNONVOLATILE_CONTEXT_POINTERS pointers)
{
    const UNWIND_INFO *info = (const UNWIND_INFO *)(base + function->UnwindData);
    ULONG64 offset = pc - base - function->BeginAddress;
    ULONG64 frame_base = 0;
    BOOLEAN have_frame = FALSE, machine_frame = FALSE;

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
        if (handler_data)
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
                machine_frame = TRUE;
                break;
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

    /* a machine frame gave the caller's rip and rsp, the frame still has its handler. */
    if (!machine_frame) {
        c->Rip = *(ULONG64 *)c->Rsp;
        c->Rsp += 8;
    }

    info = (const UNWIND_INFO *)(base + function->UnwindData);
    while (info->Flags & UNW_FLAG_CHAININFO) {
        PRUNTIME_FUNCTION chained = (PRUNTIME_FUNCTION)&info->UnwindCode[(info->CountOfCodes + 1) & ~1];
        info = (const UNWIND_INFO *)(base + chained->UnwindData);
    }
    if (info->Flags & type & (UNW_FLAG_EHANDLER | UNW_FLAG_UHANDLER)) {
        const ULONG *handler = (const ULONG *)&info->UnwindCode[(info->CountOfCodes + 1) & ~1];
        if (handler_data)
            *handler_data = (PVOID)(handler + 1);
        return (PEXCEPTION_ROUTINE)(base + *handler);
    }
    /* the Go runtime unwinds with no place for the handler data. */
    if (handler_data)
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

/* ------------------------------------------------------------ dispatch */

SEH_EXPORT BOOLEAN NTAPI RtlDispatchFrameException(PEXCEPTION_RECORD record, PCONTEXT context)
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
            DbgPrint("seh: the handler %p of frame %p returned %d for exception %08x\n", (PVOID)handler,
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
    /* a termination handler that ran is not run again. */
    dispatcher->ScopeIndex = outer->ScopeIndex;
    return ExceptionCollidedUnwind;
}

SEH_EXPORT void NTAPI RtlUnwindEx(PVOID target_frame, PVOID target_ip, PEXCEPTION_RECORD record, PVOID value,
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

    ULONG carried_scope = 0;
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
            dc.ScopeIndex = carried_scope;
            carried_scope = 0;
            EXCEPTION_DISPOSITION r = RtlpExecuteHandlerForUnwind(record, (PVOID)dc.EstablisherFrame, &c, &dc, handler);
            record->ExceptionFlags &= ~(EXCEPTION_COLLIDED_UNWIND | EXCEPTION_TARGET_UNWIND);
            if (r == ExceptionCollidedUnwind) {
                /*
                 * the handler left the context of another frame in the dispatcher context, which is c. That frame
                 * is unwound from there, and its handlers are told of the collision.
                 */
                record->ExceptionFlags |= EXCEPTION_COLLIDED_UNWIND;
                carried_scope = dc.ScopeIndex;
                continue;
            } else if (r != ExceptionContinueSearch) {
                RtlRaiseStatus(STATUS_INVALID_DISPOSITION);
            }
        }
        carried_scope = 0;
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

SEH_EXPORT EXCEPTION_DISPOSITION NTAPI __C_specific_handler(PEXCEPTION_RECORD record, PVOID frame, PCONTEXT context,
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
