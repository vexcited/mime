/*
 * wow64cpu.dll: the x64 processor of wow64. 32-bit code runs natively in
 * the 32-bit code segment and comes back for each system call, which
 * wow64.dll converts and makes. see x64/cpu.S.
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
 * runs the thread's 32-bit code, making its system calls, until a call
 * into it returns. the code reaches the 64-bit side again through the
 * gates, so they must sit below 4 GiB.
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

/* the 32-bit addresses wow64.dll gives ntdll's Wow64Transition and the calls it makes into 32-bit code. */
CPU_EXPORT ULONG NTAPI Wow64CpuGetTransition(void)
{
    return (ULONG)(ULONG_PTR)Wow64CpuTransition;
}

CPU_EXPORT ULONG NTAPI Wow64CpuGetReturnAddress(void)
{
    return (ULONG)(ULONG_PTR)Wow64CpuReturn32;
}
