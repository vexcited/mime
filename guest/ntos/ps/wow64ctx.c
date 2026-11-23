#include "../ntos.h"
#include <mime/wow32.h>

#ifdef __x86_64__

/*
 * ThreadWow64Context: the x86 context of a thread of a wow64 process, read from PspWow64GetContextThread and
 * PspWow64SetContextThread of ntoskrnl 26300. a thread that runs 32-bit code has its x86 registers in its 64-bit
 * context. one in the 64-bit layer has them in its cpu area, where wow64cpu keeps the integer and control registers
 * only, until the flag RESET_STATE of the area tells the area holds the floating point state too. the kernel then
 * fills that state from the 64-bit context, with xmm0 to xmm5 cleared since 64-bit code does not keep them.
 */

#define X86_CONTROL 0x10001
#define X86_INTEGER 0x10002
#define X86_SEGMENTS 0x10004
#define X86_FLOATING_POINT 0x10008
#define X86_DEBUG_REGISTERS 0x10010
#define X86_EXTENDED_REGISTERS 0x10020
#define X86_XSTATE 0x10040
#define X86_CONTEXT_LENGTH 0x2CC
#define WOW64_X86_FEATURES 0x40000000000009FCull

static BOOLEAN has(ULONG flags, ULONG part)
{
    return (flags & part) == part;
}

/* RtlFxToFnFrame: the fxsave image of the x87 state as the fnsave one of an x86 context. */
static void fx_to_fn(const UCHAR *fx, PUCHAR fn)
{
    USHORT status = *(const USHORT *)(fx + 2);
    UCHAR abridged = fx[4];
    ULONG top = (status >> 11) & 7, tags = 0;
    *(PULONG)(fn + 0) = *(const USHORT *)(fx + 0);
    *(PULONG)(fn + 4) = status;
    *(PULONG)(fn + 12) = *(const ULONG *)(fx + 8);
    *(PULONG)(fn + 16) = *(const USHORT *)(fx + 12) | (ULONG)*(const USHORT *)(fx + 6) << 16;
    *(PULONG)(fn + 20) = *(const ULONG *)(fx + 16);
    *(PULONG)(fn + 24) = *(const USHORT *)(fx + 20);
    for (ULONG i = 0; i < 8; i++)
        memcpy(fn + 28 + 10 * i, fx + 32 + 16 * i, 10);
    for (LONG physical = 7; physical >= 0; physical--) {
        ULONG tag = 3;
        if (abridged >> physical & 1) {
            const UCHAR *reg = fx + 32 + 16 * ((physical - top) & 7);
            ULONG64 mantissa = *(const ULONG64 *)reg;
            USHORT exponent = *(const USHORT *)(reg + 8) & 0x7FFF;
            if (!exponent)
                tag = mantissa ? 2 : 1;
            else if (exponent == 0x7FFF || !(mantissa >> 63))
                tag = 2;
            else
                tag = 0;
        }
        tags = tags << 2 | tag;
    }
    *(PULONG)(fn + 8) = tags;
}

/* RtlFnToFxFrame: the fnsave image back into the x87 part of an fxsave one. */
static void fn_to_fx(const UCHAR *fn, PUCHAR fx)
{
    USHORT tags = *(const USHORT *)(fn + 8);
    UCHAR abridged = 0;
    *(PUSHORT)(fx + 0) = *(const USHORT *)(fn + 0);
    *(PUSHORT)(fx + 2) = *(const USHORT *)(fn + 4);
    *(PULONG)(fx + 8) = *(const ULONG *)(fn + 12);
    *(PUSHORT)(fx + 12) = *(const USHORT *)(fn + 16);
    *(PUSHORT)(fx + 6) = *(const USHORT *)(fn + 18);
    *(PULONG)(fx + 16) = *(const ULONG *)(fn + 20);
    *(PUSHORT)(fx + 20) = *(const USHORT *)(fn + 24);
    for (ULONG i = 0; i < 8; i++) {
        memcpy(fx + 32 + 16 * i, fn + 28 + 10 * i, 10);
        abridged = (UCHAR)(abridged << 1 | ((tags & 0xC000) != 0xC000));
        tags = (USHORT)(tags << 2);
    }
    fx[4] = abridged;
}

/* RtlpWow64CtxFromAmd64: the parts of an x86 context the flags name, from the 64-bit context of the thread. */
static void ctx_from_amd64(ULONG flags, const CONTEXT *a, I386_CONTEXT *x)
{
    ULONG got = x->ContextFlags;
    if (has(flags, X86_CONTROL)) {
        x->Ebp = (ULONG)a->Rbp;
        x->SegCs = 0x23;
        x->Eip = (ULONG)a->Rip;
        x->SegSs = 0x2B;
        x->Esp = (ULONG)a->Rsp;
        x->EFlags = a->EFlags;
        got |= X86_CONTROL;
    }
    if (has(flags, X86_INTEGER)) {
        x->Edi = (ULONG)a->Rdi;
        x->Esi = (ULONG)a->Rsi;
        x->Ebx = (ULONG)a->Rbx;
        x->Edx = (ULONG)a->Rdx;
        x->Ecx = (ULONG)a->Rcx;
        x->Eax = (ULONG)a->Rax;
        got |= X86_INTEGER;
    }
    if (has(flags, X86_SEGMENTS)) {
        x->SegGs = x->SegEs = x->SegDs = 0x2B;
        x->SegFs = 0x53;
        got |= X86_SEGMENTS;
    }
    if (has(flags, X86_EXTENDED_REGISTERS)) {
        memcpy(x->ExtendedRegisters, &a->FltSave, sizeof(x->ExtendedRegisters));
        got |= X86_EXTENDED_REGISTERS;
    }
    if (has(flags, X86_FLOATING_POINT)) {
        x->FloatSave.Cr0NpxState = 0;
        fx_to_fn((const UCHAR *)&a->FltSave, (PUCHAR)&x->FloatSave);
        got |= X86_FLOATING_POINT;
    }
    if (has(flags, X86_DEBUG_REGISTERS)) {
        if (a->Dr7 & 0x355) {
            x->Dr0 = (ULONG)a->Dr0;
            x->Dr1 = (ULONG)a->Dr1;
            x->Dr2 = (ULONG)a->Dr2;
            x->Dr3 = (ULONG)a->Dr3;
            x->Dr6 = (ULONG)a->Dr6;
            x->Dr7 = (ULONG)a->Dr7 & 0xFFFF0155;
        } else {
            x->Dr0 = x->Dr1 = x->Dr2 = x->Dr3 = x->Dr6 = x->Dr7 = 0;
        }
        got |= X86_DEBUG_REGISTERS;
    }
    x->ContextFlags = got;
}

/*
 * RtlpWow64CtxToAmd64: the parts of a 64-bit context an x86 one gives. a thread in the 64-bit layer takes only its
 * floating point and debug state that way. returns the parts written.
 */
static ULONG ctx_to_amd64(ULONG flags, const I386_CONTEXT *x, CONTEXT *a)
{
    ULONG parts = 0, given = x->ContextFlags;
    if (a->SegCs != 0x23)
        flags = (flags & 0xFFFEFFF8) | CONTEXT_I386;
    if (has(flags, X86_CONTROL) && has(given, X86_CONTROL)) {
        a->SegCs = 0x23;
        a->SegSs = 0x2B;
        a->Rip = x->Eip;
        a->Rbp = x->Ebp;
        a->Rsp = x->Esp;
        a->EFlags = x->EFlags;
        parts |= CONTEXT_CONTROL;
    }
    if (has(flags, X86_INTEGER) && has(given, X86_INTEGER)) {
        a->Rdi = x->Edi;
        a->Rsi = x->Esi;
        a->Rbx = x->Ebx;
        a->Rdx = x->Edx;
        a->Rcx = x->Ecx;
        a->Rax = x->Eax;
        parts |= CONTEXT_INTEGER;
    }
    if (has(flags, X86_EXTENDED_REGISTERS) && has(given, X86_EXTENDED_REGISTERS)) {
        memcpy(&a->FltSave, x->ExtendedRegisters, sizeof(x->ExtendedRegisters));
        a->MxCsr = a->FltSave.MxCsr;
        parts |= CONTEXT_FLOATING_POINT;
    }
    if (has(flags, X86_FLOATING_POINT) && has(given, X86_FLOATING_POINT)) {
        fn_to_fx((const UCHAR *)&x->FloatSave, (PUCHAR)&a->FltSave);
        parts |= CONTEXT_FLOATING_POINT;
    }
    if (has(flags & given, X86_DEBUG_REGISTERS)) {
        a->Dr0 = x->Dr0;
        a->Dr1 = x->Dr1;
        a->Dr2 = x->Dr2;
        a->Dr3 = x->Dr3;
        a->Dr6 = x->Dr6;
        a->Dr7 = x->Dr7;
        if (!(x->Dr7 & 0x55) && (x->Dr0 || x->Dr1 || x->Dr2 || x->Dr3))
            a->Dr7 |= 0x100;
        parts |= CONTEXT_DEBUG_REGISTERS;
    }
    return parts;
}

/* RtlpCopyLegacyContextX86 under RtlCopyContext: the parts both contexts have, the rest of the target stays. */
static void copy_x86(I386_CONTEXT *to, ULONG flags, const I386_CONTEXT *from)
{
    ULONG parts = flags & from->ContextFlags;
    if (has(parts, X86_CONTROL)) {
        to->Ebp = from->Ebp;
        to->Eip = from->Eip;
        to->SegCs = from->SegCs;
        to->EFlags = from->EFlags;
        to->Esp = from->Esp;
        to->SegSs = from->SegSs;
    }
    if (has(parts, X86_INTEGER)) {
        to->Edi = from->Edi;
        to->Esi = from->Esi;
        to->Ebx = from->Ebx;
        to->Edx = from->Edx;
        to->Ecx = from->Ecx;
        to->Eax = from->Eax;
    }
    if (has(parts, X86_SEGMENTS)) {
        to->SegGs = from->SegGs;
        to->SegFs = from->SegFs;
        to->SegEs = from->SegEs;
        to->SegDs = from->SegDs;
    }
    if (has(parts, X86_FLOATING_POINT))
        memcpy(&to->FloatSave, &from->FloatSave, sizeof(to->FloatSave));
    if (has(parts, X86_DEBUG_REGISTERS)) {
        to->Dr0 = from->Dr0;
        to->Dr1 = from->Dr1;
        to->Dr2 = from->Dr2;
        to->Dr3 = from->Dr3;
        to->Dr6 = from->Dr6;
        to->Dr7 = from->Dr7;
    }
    if (has(parts, X86_EXTENDED_REGISTERS))
        memcpy(to->ExtendedRegisters, from->ExtendedRegisters, sizeof(to->ExtendedRegisters));
    to->ContextFlags = parts & 0x67FFFFFF;
}

/*
 * RtlpWow64SanitizeContextFlags for a caller in user mode: flags that are not an x86 context's become one. the
 * extended state of an x86 context, which needs the CONTEXT_EX that follows it, is not served here yet.
 */
static ULONG sanitize(ULONG flags)
{
    return ((flags | CONTEXT_I386) & 0xD801007F) & ~(ULONG)0x40;
}

/* the cpu area of a thread, read or written in its process: the flags word and the x86 context. */
static NTSTATUS cpu_area(PETHREAD thread, PUSHORT flags, I386_CONTEXT *context, BOOLEAN write)
{
    PEPROCESS process = (PEPROCESS)thread->Tcb.Process;
    PTEB teb = thread->Tcb.Teb;
    if (!teb)
        return STATUS_THREAD_IS_TERMINATING;
    PKPROCESS saved = KiCurrentThread()->ApcStateProcess;
    KiAttachProcess(&process->Pcb);
    PWOW64_CPURESERVED area = nullptr;
    NTSTATUS status = KiCopyFromUser(&area, &teb->TlsSlots[WOW64_TLS_CPURESERVED], sizeof(area));
    if (NT_SUCCESS(status) && !area)
        status = STATUS_INVALID_PARAMETER;
    if (NT_SUCCESS(status) && write) {
        status = KiCopyToUser(&area->Context, context, X86_CONTEXT_LENGTH);
        if (NT_SUCCESS(status))
            status = KiCopyToUser(&area->Flags, flags, sizeof(*flags));
    } else if (NT_SUCCESS(status)) {
        status = KiCopyFromUser(context, &area->Context, X86_CONTEXT_LENGTH);
        if (NT_SUCCESS(status))
            status = KiCopyFromUser(flags, &area->Flags, sizeof(*flags));
    }
    KiAttachProcess(saved);
    return status;
}

/* the floating point state of a thread in the 64-bit layer, taken into its area the first time it is asked for. */
static void complete_area(const CONTEXT *a, I386_CONTEXT *area, PUSHORT flags)
{
    ULONG parts = X86_FLOATING_POINT | X86_DEBUG_REGISTERS | X86_EXTENDED_REGISTERS;
    ctx_from_amd64(parts, a, area);
    memset(area->ExtendedRegisters + 0xA0, 0, 0x60);
    *flags |= WOW64_CPURESERVED_FLAG_RESET_STATE;
}

static NTSTATUS begin(PETHREAD thread, ULONG length, BOOLEAN *suspended)
{
    *suspended = FALSE;
    if (length != X86_CONTEXT_LENGTH)
        return STATUS_INFO_LENGTH_MISMATCH;
    if (!PspIsWow64Process((PEPROCESS)thread->Tcb.Process))
        return STATUS_INVALID_PARAMETER;
    if (thread != PsCurrentThread()) {
        if (!NT_SUCCESS(PspSuspendThread(thread)))
            return STATUS_SUCCESS;
        *suspended = TRUE;
    }
    return STATUS_SUCCESS;
}

NTSTATUS PspWow64GetContextThread(PETHREAD thread, PVOID context, ULONG length)
{
    BOOLEAN suspended;
    NTSTATUS status = begin(thread, length, &suspended);
    if (!NT_SUCCESS(status))
        return status;
    I386_CONTEXT out, area;
    CONTEXT a;
    USHORT area_flags = 0;
    ULONG report = 0;
    status = KiCopyFromUser(&out, context, sizeof(out));
    ULONG flags = sanitize(out.ContextFlags);
    if (NT_SUCCESS(status))
        status = PsGetUserContextThread(thread, &a, &report);
    if (NT_SUCCESS(status))
        status = cpu_area(thread, &area_flags, &area, FALSE);
    if (NT_SUCCESS(status)) {
        out.ContextFlags = 0;
        if (a.SegCs == 0x23) {
            ctx_from_amd64(flags, &a, &out);
        } else {
            if (!(area_flags & WOW64_CPURESERVED_FLAG_RESET_STATE)) {
                complete_area(&a, &area, &area_flags);
                status = cpu_area(thread, &area_flags, &area, TRUE);
            }
            copy_x86(&out, flags, &area);
        }
        if (flags & CONTEXT_EXCEPTION_REQUEST) {
            out.ContextFlags = (out.ContextFlags & 0x67FFFFFF) | (report & ~0x67FFFFFFu);
            if (!(out.ContextFlags & (CONTEXT_EXCEPTION_ACTIVE | CONTEXT_SERVICE_ACTIVE)))
                out.ContextFlags |= CONTEXT_EXCEPTION_REPORTING | CONTEXT_EXCEPTION_ACTIVE;
        }
    }
    if (NT_SUCCESS(status))
        status = KiCopyToUser(context, &out, sizeof(out));
    if (suspended)
        PspResumeThread(thread);
    return status;
}

NTSTATUS PspWow64SetContextThread(PETHREAD thread, PVOID context, ULONG length)
{
    BOOLEAN suspended;
    NTSTATUS status = begin(thread, length, &suspended);
    if (!NT_SUCCESS(status))
        return status;
    I386_CONTEXT in, area;
    CONTEXT a;
    USHORT area_flags = 0;
    ULONG report;
    status = KiCopyFromUser(&in, context, sizeof(in));
    ULONG flags = sanitize(in.ContextFlags) & 0x37FFFFFF;
    in.ContextFlags = flags;
    if (NT_SUCCESS(status))
        status = PsGetUserContextThread(thread, &a, &report);
    if (NT_SUCCESS(status))
        status = cpu_area(thread, &area_flags, &area, FALSE);
    ULONG parts = 0;
    if (NT_SUCCESS(status) && a.SegCs == 0x23) {
        parts = ctx_to_amd64(flags, &in, &a);
    } else if (NT_SUCCESS(status)) {
        if (!(area_flags & WOW64_CPURESERVED_FLAG_RESET_STATE))
            complete_area(&a, &area, &area_flags);
        ULONG kept = area.ContextFlags;
        copy_x86(&area, flags, &in);
        area.ContextFlags |= kept;
        if (has(flags, X86_DEBUG_REGISTERS))
            parts = ctx_to_amd64(X86_DEBUG_REGISTERS, &in, &a);
        status = cpu_area(thread, &area_flags, &area, TRUE);
    }
    if (NT_SUCCESS(status) && parts)
        status = PsSetUserContextThread(thread, &a, CONTEXT_AMD64 | parts);
    if (suspended)
        PspResumeThread(thread);
    return status;
}
#else
/* a 32-bit program on arm64 runs in xta64, which keeps its x86 context to itself: no process is a wow64 one. */
NTSTATUS PspWow64GetContextThread(PETHREAD thread, PVOID context, ULONG length)
{
    return length != 0x2CC ? STATUS_INFO_LENGTH_MISMATCH : STATUS_INVALID_PARAMETER;
}

NTSTATUS PspWow64SetContextThread(PETHREAD thread, PVOID context, ULONG length)
{
    return length != 0x2CC ? STATUS_INFO_LENGTH_MISMATCH : STATUS_INVALID_PARAMETER;
}
#endif
