/*
 * runs WOW64 code natively in the 32-bit code segment. system calls return through x64/cpu.S to wow64.dll for
 * conversion.
 */
#include <mime/ntrtl.h>

#define CPU_EXPORT __declspec(dllexport)

ULONG Wow64CpuRun(void);
extern UCHAR Wow64CpuTransition[];
extern UCHAR Wow64CpuReturn32[];

__declspec(dllimport) LONG NTAPI Wow64SystemServiceEx(ULONG number, PULONG arguments);

static PWOW64_CPURESERVED cpu_area(void)
{
    return NtCurrentTeb()->TlsSlots[WOW64_TLS_CPURESERVED];
}

BOOL WINAPI DllMain(PVOID instance, ULONG reason, PVOID reserved)
{
    return TRUE;
}

/*
 * runs 32-bit code until the call returns, dispatching system calls through the gates. the gates must reside below 4
 * GiB.
 */
CPU_EXPORT void NTAPI BTCpuSimulate(void)
{
    PWOW64_CPURESERVED cpu = cpu_area();
    while (!Wow64CpuRun()) {
        ULONG number = cpu->Context.Eax;
        cpu->Context.Eax = (ULONG)Wow64SystemServiceEx(number, (PULONG)(ULONG_PTR)(cpu->Context.Esp + 4));
    }
}

CPU_EXPORT NTSTATUS NTAPI BTCpuProcessInit(PCWSTR image, SIZE_T *thread_size)
{
    if (thread_size)
        *thread_size = 0;
    if ((ULONG_PTR)Wow64CpuTransition >> 32)
        return STATUS_CONFLICTING_ADDRESSES;
    return STATUS_SUCCESS;
}

CPU_EXPORT NTSTATUS NTAPI BTCpuThreadInit(void)
{
    return STATUS_SUCCESS;
}

/* 32-bit gate addresses for ntdll's Wow64Transition and calls from wow64.dll. */
CPU_EXPORT ULONG NTAPI Wow64CpuGetTransition(void)
{
    return (ULONG)(ULONG_PTR)Wow64CpuTransition;
}

CPU_EXPORT ULONG NTAPI Wow64CpuGetReturnAddress(void)
{
    return (ULONG)(ULONG_PTR)Wow64CpuReturn32;
}
