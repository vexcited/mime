/*
 * the return path to user mode. the interrupt and exception handlers are
 * in arch/<processor>/trap.c.
 */
#include "../ntos.h"

BOOLEAN KiTraceUserExceptions;

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
