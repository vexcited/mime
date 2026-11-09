/*
 * the return path to user mode. the interrupt and exception handlers are
 * in arch/<processor>/trap.c.
 */
#include "../ntos.h"

BOOLEAN KiTraceUserExceptions;

/* the image and offset of a user address in the current process, as "name+0xoffset". */
static BOOLEAN describe_in(PEPROCESS process, ULONG_PTR va, char *out, ULONG size)
{
    PMMADDRESS_SPACE as = &process->AddressSpace;
    if (va > MIME_USER_ADDRESS_LIMIT)
        return FALSE;
    ExAcquireFastMutex(&as->Lock);
    PMMVAD vad = MiFindVad(as, va);
    BOOLEAN found = vad && vad->Type == MiVadImage && vad->Section && vad->Section->FileName.Length;
    if (found) {
        PCWSTR name = vad->Section->FileName.Buffer;
        ULONG count = vad->Section->FileName.Length / sizeof(WCHAR), first = 0;
        for (ULONG i = 0; i < count; i++) {
            if (name[i] == '\\')
                first = i + 1;
        }
        ULONG n = 0;
        for (ULONG i = first; i < count && n + 24 < size; i++)
            out[n++] = name[i] < 0x80 ? (char)name[i] : '?';
        out[n] = 0;
        mime_format(out + n, size - n, "+0x%llx", (ULONG64)(va - vad->StartVa));
    }
    ExReleaseFastMutex(&as->Lock);
    return found;
}

BOOLEAN KiDescribeUserAddress(ULONG_PTR va, char *out, ULONG size)
{
    return describe_in(PsCurrentProcess(), va, out, size);
}

/*
 * one line with the code addresses of a thread's stack that point into images, innermost first, for the thread
 * dump. the caller is attached to the process, which owns the address.
 */
void KiFormatUserStack(PEPROCESS process, ULONG64 sp, char *out, ULONG size)
{
    ULONG n = 0;
    out[0] = 0;
    char text[96];
    for (ULONG i = 0, shown = 0; i < 1024 && shown < 14 && sp && !(sp & 7); i++, sp += 8) {
        ULONG64 word;
        if (!MmIsAddressValid((PVOID)sp) || !NT_SUCCESS(KiCopyFromUser(&word, (PVOID)sp, sizeof(word))))
            break;
        if (describe_in(process, word, text, sizeof(text))) {
            ULONG length = (ULONG)strlen(text);
            if (n + length + 2 >= size)
                break;
            out[n++] = ' ';
            memcpy(out + n, text, length + 1);
            n += length;
            shown++;
        }
    }
}

/*
 * an unresolved access is an access violation unless the memory manager says more. like windows, a guard page or the
 * end of a stack keeps its own code with access violation parameters, and any other failure becomes
 * STATUS_IN_PAGE_ERROR with the status as the third parameter. the record is changed in place.
 */
void KiApplyFaultStatus(PEXCEPTION_RECORD record)
{
    PKTHREAD thread = KiCurrentThread();
    NTSTATUS status = thread->FaultStatus;
    if (!status || record->ExceptionCode != STATUS_ACCESS_VIOLATION || record->NumberParameters < 2)
        return;
    thread->FaultStatus = 0;
    if (status == STATUS_GUARD_PAGE_VIOLATION || status == STATUS_STACK_OVERFLOW) {
        record->ExceptionCode = status;
        return;
    }
    record->ExceptionCode = STATUS_IN_PAGE_ERROR;
    record->NumberParameters = 3;
    record->ExceptionInformation[2] = (ULONG)status;
}

/* the images of the instruction pointer and of the words on top of the stack that point into one. */
void KiTraceUserStack(ULONG64 pc, ULONG64 sp)
{
    char text[96];
    if (KiDescribeUserAddress(pc, text, sizeof(text)))
        KiLogPrintf("  at %s", text);
    ULONG shown = 0;
    /* the stack of a 32-bit program lies below 4 GiB and holds words of four bytes. */
    ULONG step = sp && sp < 0x100000000ull ? 4 : 8;
    for (ULONG i = 0; i < 512 && shown < 24 && sp && !(sp & (step - 1)); i++, sp += step) {
        ULONG64 word = 0;
        if (!MmIsAddressValid((PVOID)sp) || !NT_SUCCESS(KiCopyFromUser(&word, (PVOID)sp, step)))
            break;
        if (KiDescribeUserAddress(word, text, sizeof(text))) {
            KiLogPrintf("  stack +0x%x: %s", i * step, text);
            shown++;
        }
    }
}

#ifdef __x86_64__
/* the base of the image that holds a user address of the current process, or zero. */
static ULONG64 image_base_of(ULONG64 va)
{
    PMMADDRESS_SPACE as = &PsCurrentProcess()->AddressSpace;
    ULONG64 base = 0;
    if (va > MIME_USER_ADDRESS_LIMIT)
        return 0;
    ExAcquireFastMutex(&as->Lock);
    PMMVAD vad = MiFindVad(as, va);
    if (vad && vad->Type == MiVadImage)
        base = vad->StartVa;
    ExReleaseFastMutex(&as->Lock);
    return base;
}

static BOOLEAN read_user(ULONG64 va, void *out, ULONG size)
{
    return NT_SUCCESS(KiCopyFromUser(out, (PVOID)va, size));
}

/* the runtime function of a code address in the unwind table of its image, three rvas. */
static BOOLEAN find_function(ULONG64 base, ULONG64 pc, ULONG function[3])
{
    ULONG e_lfanew, table[2];
    if (!read_user(base + 0x3c, &e_lfanew, 4) || !read_user(base + e_lfanew + 0xa0, table, 8) || !table[0])
        return FALSE;
    ULONG rva = (ULONG)(pc - base);
    LONG low = 0, high = (LONG)(table[1] / 12) - 1;
    while (low <= high) {
        LONG mid = (low + high) / 2;
        if (!read_user(base + table[0] + (ULONG64)mid * 12, function, 12))
            return FALSE;
        if (rva < function[0])
            high = mid - 1;
        else if (rva >= function[1])
            low = mid + 1;
        else if (function[2] & 1)
            /* a cold part has the entry of the function it belongs to, whose prolog ran completely. */
            return read_user(base + (function[2] & ~1u), function, 12);
        else
            return TRUE;
    }
    return FALSE;
}

/* the frames of the stack by the unwind data of the images, so a call without a frame pointer shows up. */
static void trace_unwound(PKTRAP_FRAME frame)
{
    ULONG64 r[16] = {frame->Rax, frame->Rcx, frame->Rdx, frame->Rbx, frame->Rsp, frame->Rbp, frame->Rsi, frame->Rdi,
                     frame->R8,  frame->R9,  frame->R10, frame->R11, frame->R12, frame->R13, frame->R14, frame->R15};
    ULONG64 pc = frame->Rip;
    char text[96];
    for (ULONG depth = 0; depth < 32; depth++) {
        if (KiDescribeUserAddress(pc, text, sizeof(text)))
            KiLogPrintf("  frame %u: %s", depth, text);
        else
            KiLogPrintf("  frame %u: %llx", depth, pc);
        ULONG64 base = image_base_of(pc);
        ULONG function[3];
        if (base && find_function(base, pc, function)) {
            ULONG offset = (ULONG)(pc - base) - function[0];
            if (pc - base < function[0] || pc - base >= function[1])
                offset = 0xffffffff;
            for (;;) {
                UCHAR header[4];
                if (!read_user(base + function[2], header, 4))
                    return;
                ULONG count = header[2];
                UCHAR codes[512];
                if (count * 2 > sizeof(codes) || !read_user(base + function[2] + 4, codes, count * 2))
                    return;
                for (ULONG i = 0; i < count;) {
                    UCHAR at = codes[i * 2], op = codes[i * 2 + 1] & 15, info = codes[i * 2 + 1] >> 4;
                    ULONG slots = 1;
                    ULONG64 value = 0;
                    if (op == 1)
                        slots = info ? 3 : 2;
                    else if (op == 4 || op == 8 || op == 6 || op == 7)
                        slots = 2;
                    else if (op == 5 || op == 9)
                        slots = 3;
                    if (i + slots > count)
                        return;
                    if (at <= offset) {
                        USHORT w1 = *(USHORT *)&codes[(i + 1) * 2];
                        switch (op) {
                        case 0:
                            if (!read_user(r[4], &value, 8))
                                return;
                            r[info] = value;
                            r[4] += 8;
                            break;
                        case 1:
                            r[4] += info ? *(ULONG *)&codes[(i + 1) * 2] : (ULONG64)w1 * 8;
                            break;
                        case 2:
                            r[4] += (ULONG64)info * 8 + 8;
                            break;
                        case 3:
                            r[4] = r[header[3] & 15] - (ULONG64)(header[3] >> 4) * 16;
                            break;
                        case 4:
                            if (!read_user(r[4] + (ULONG64)w1 * 8, &value, 8))
                                return;
                            r[info] = value;
                            break;
                        case 5:
                            if (!read_user(r[4] + *(ULONG *)&codes[(i + 1) * 2], &value, 8))
                                return;
                            r[info] = value;
                            break;
                        case 10:
                            KiLogPrintf("  machine frame");
                            return;
                        default:
                            break;
                        }
                    }
                    i += slots;
                }
                /* a chained entry continues with the unwind data of the function it belongs to. */
                if (!(header[0] >> 3 & 4))
                    break;
                ULONG chained[3];
                if (!read_user(base + function[2] + 4 + ((count + 1) & ~1u) * 2, chained, 12))
                    return;
                memcpy(function, chained, 12);
                offset = 0xffffffff;
            }
        }
        ULONG64 ret;
        if (!read_user(r[4], &ret, 8) || !ret)
            return;
        pc = ret;
        r[4] += 8;
    }
}
#endif

/* the frames of the stack of a thread that is in a system call. */
void KiTraceUserFrames(PKTRAP_FRAME frame)
{
#ifdef __x86_64__
    if (KI_TRAP_SP(frame) >= 0x100000000ull) {
        trace_unwound(frame);
        return;
    }
#endif
    KiTraceUserStack(KI_TRAP_PC(frame), KI_TRAP_SP(frame));
}

/*
 * final processing before returning to user mode: kernel apcs, thread
 * termination, user apcs and preemption. returns with interrupts masked.
 */
void KiExitToUserMode(PKTRAP_FRAME frame)
{
    PKTHREAD thread = KiCurrentThread();
    for (;;) {
        KiDisableInterrupts();
        if (thread->KernelApcPending && !thread->KernelApcDisable && !thread->SpecialApcDisable) {
            KiEnableInterrupts();
            KiDeliverApcs(KernelMode, frame);
            continue;
        }
        if (thread->Terminating) {
            KiEnableInterrupts();
            PspExitThread(((PETHREAD)thread)->ExitStatus);
        }
        if (thread->UserApcPending) {
            KiEnableInterrupts();
            KiDeliverApcs(UserMode, frame);
            continue;
        }
        if (KeGetCurrentPrcb()->DispatchPending || KeGetCurrentPrcb()->DpcPending) {
            KiDispatchPendingWork();
            continue;
        }
        break;
    }
    if (thread->InstrumentReturn) {
        thread->InstrumentReturn = FALSE;
        KiInstrumentReturn(frame, ((PEPROCESS)thread->Process)->InstrumentationCallback);
    }
    thread->PreviousMode = UserMode;
}
