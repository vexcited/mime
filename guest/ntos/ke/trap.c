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

/* the images of the instruction pointer and of the words on top of the stack that point into one. */
void KiTraceUserStack(ULONG64 pc, ULONG64 sp)
{
    char text[96];
    if (KiDescribeUserAddress(pc, text, sizeof(text)))
        KiLogPrintf("  at %s", text);
    ULONG shown = 0;
    for (ULONG i = 0; i < 512 && shown < 24 && sp && !(sp & 7); i++, sp += 8) {
        ULONG64 word;
        if (!MmIsAddressValid((PVOID)sp) || !NT_SUCCESS(KiCopyFromUser(&word, (PVOID)sp, sizeof(word))))
            break;
        if (KiDescribeUserAddress(word, text, sizeof(text))) {
            KiLogPrintf("  stack +0x%x: %s", i * 8, text);
            shown++;
        }
    }
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
    thread->PreviousMode = UserMode;
}
