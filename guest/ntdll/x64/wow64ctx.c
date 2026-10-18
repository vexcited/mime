/*
 * the 32-bit context of a wow64 thread, seen from 64-bit code: the
 * processor's registers while it runs 32-bit code, else the cpu area its
 * teb points at.
 */
#include "../ntdll_private.h"

#define CODE32 0x23
#define DATA32 0x2B
#define TEB32_SEGMENT 0x53

typedef struct WOW64_TARGET {
    HANDLE Process;
    ULONG_PTR Area;
} WOW64_TARGET;

static NTSTATUS open_target(HANDLE thread, WOW64_TARGET *target)
{
    THREAD_BASIC_INFORMATION info;
    NTSTATUS status = NtQueryInformationThread(thread, ThreadBasicInformation, &info, sizeof(info), nullptr);
    if (!NT_SUCCESS(status))
        return status;
    OBJECT_ATTRIBUTES oa;
    InitializeObjectAttributes(&oa, nullptr, 0, nullptr, nullptr);
    CLIENT_ID id = {info.ClientId.UniqueProcess, nullptr};
    status = NtOpenProcess(&target->Process, PROCESS_VM_READ | PROCESS_VM_WRITE | PROCESS_VM_OPERATION |
                                                 PROCESS_QUERY_INFORMATION, &oa, &id);
    if (!NT_SUCCESS(status))
        return status;
    PTEB teb = info.TebBaseAddress;
    status = NtReadVirtualMemory(target->Process, &teb->TlsSlots[WOW64_TLS_CPURESERVED], &target->Area,
                                 sizeof(target->Area), nullptr);
    if (NT_SUCCESS(status) && !target->Area)
        status = STATUS_INVALID_PARAMETER;
    if (!NT_SUCCESS(status))
        NtClose(target->Process);
    return status;
}

static void fix_segments(I386_CONTEXT *c)
{
    c->SegCs = CODE32;
    c->SegDs = c->SegEs = c->SegSs = DATA32;
    c->SegFs = TEB32_SEGMENT;
    c->SegGs = 0;
}

static NTSTATUS wow64_context(HANDLE thread, I386_CONTEXT *context, BOOLEAN setting)
{
    WOW64_TARGET target;
    NTSTATUS status = open_target(thread, &target);
    if (!NT_SUCCESS(status))
        return status;
    CONTEXT native;
    memset(&native, 0, sizeof(native));
    native.ContextFlags = CONTEXT_FULL | CONTEXT_SEGMENTS;
    status = NtGetContextThread(thread, &native);
    if (NT_SUCCESS(status) && native.SegCs == CODE32) {
        /* the thread stopped in 32-bit code: its registers are the processor's. */
        ULONG *regs[] = {&context->Eax, &context->Ebx, &context->Ecx, &context->Edx, &context->Esi,
                         &context->Edi, &context->Ebp, &context->Esp, &context->Eip};
        ULONG64 *natives[] = {&native.Rax, &native.Rbx, &native.Rcx, &native.Rdx, &native.Rsi,
                              &native.Rdi, &native.Rbp, &native.Rsp, &native.Rip};
        if (!setting) {
            for (int i = 0; i < 9; i++)
                *regs[i] = (ULONG)*natives[i];
            context->EFlags = native.EFlags;
            fix_segments(context);
        } else {
            for (int i = 0; i < 9; i++)
                *natives[i] = *regs[i];
            native.EFlags = context->EFlags;
            status = NtSetContextThread(thread, &native);
        }
        NtClose(target.Process);
        return status;
    }
    WOW64_CPURESERVED cpu;
    status = NtReadVirtualMemory(target.Process, (PVOID)target.Area, &cpu, sizeof(cpu), nullptr);
    if (NT_SUCCESS(status) && !setting) {
        ULONG flags = context->ContextFlags;
        *context = cpu.Context;
        context->ContextFlags = flags;
        fix_segments(context);
    } else if (NT_SUCCESS(status)) {
        ULONG flags = context->ContextFlags;
        I386_CONTEXT *c = &cpu.Context;
        if ((flags & CONTEXT_I386_INTEGER) == CONTEXT_I386_INTEGER) {
            c->Edi = context->Edi;
            c->Esi = context->Esi;
            c->Ebx = context->Ebx;
            c->Edx = context->Edx;
            c->Ecx = context->Ecx;
            c->Eax = context->Eax;
        }
        if ((flags & CONTEXT_I386_CONTROL) == CONTEXT_I386_CONTROL) {
            c->Ebp = context->Ebp;
            c->Eip = context->Eip;
            c->EFlags = context->EFlags;
            c->Esp = context->Esp;
        }
        if ((flags & CONTEXT_I386_FLOATING_POINT) == CONTEXT_I386_FLOATING_POINT)
            c->FloatSave = context->FloatSave;
        if ((flags & CONTEXT_I386_EXTENDED_REGISTERS) == CONTEXT_I386_EXTENDED_REGISTERS)
            memcpy(c->ExtendedRegisters, context->ExtendedRegisters, sizeof(c->ExtendedRegisters));
        cpu.Flags |= WOW64_CPURESERVED_FLAG_RESET_STATE;
        status = NtWriteVirtualMemory(target.Process, (PVOID)target.Area, &cpu, sizeof(cpu), nullptr);
    }
    NtClose(target.Process);
    return status;
}

NTSTATUS NTAPI RtlWow64GetThreadContext(HANDLE thread, I386_CONTEXT *context)
{
    return wow64_context(thread, context, FALSE);
}

NTSTATUS NTAPI RtlWow64SetThreadContext(HANDLE thread, const I386_CONTEXT *context)
{
    return wow64_context(thread, (I386_CONTEXT *)context, TRUE);
}
