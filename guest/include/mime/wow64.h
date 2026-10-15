/*
 * the interface between mime's wow64 dlls: wow64.dll runs a 32-bit program
 * on x64, wow64cpu.dll switches the processor between 64-bit and 32-bit
 * code, and wow64win.dll converts win32k calls and window callbacks.
 */
#ifndef MIME_WOW64_H
#define MIME_WOW64_H

#include <mime/nt.h>

/* the 64-bit teb's tls slots wow64 keeps, windows' numbers: the 64-bit stack, the cpu area, the wow64 info. */
#define WOW64_TLS_STACKPTR64 0
#define WOW64_TLS_THREAD 3 /* mime's per thread state of wow64.dll */

/* the 32-bit teb of the running wow64 thread. */
FORCEINLINE PUCHAR Wow64CurrentTeb32(void)
{
    PTEB teb = NtCurrentTeb();
    return (PUCHAR)teb + teb->WowTebOffset;
}

FORCEINLINE PWOW64_CPURESERVED Wow64CpuArea(void)
{
    return NtCurrentTeb()->TlsSlots[WOW64_TLS_CPURESERVED];
}

#ifdef MIME_WOW64_DLL
#define WOW64API __declspec(dllexport)
#else
#define WOW64API __declspec(dllimport)
#endif

/*
 * calls the 32-bit ntdll's KiUserCallbackDispatcher(api, arguments, length)
 * in a simulation of its own, on the 32-bit stack below the code waiting in
 * a system call. returns its NtCallbackReturn: the status, and the 32-bit
 * address and length of its result.
 */
WOW64API NTSTATUS NTAPI Wow64KiUserCallbackDispatcher(ULONG api, ULONG arguments, ULONG length, PULONG result,
                                                      PULONG result_length);

/* wow64win.dll: one win32k system call of 32-bit code, then the window callbacks. */
typedef ULONG(NTAPI *WOW64WIN_SYSTEM_CALL)(ULONG number, const ULONG *arguments);
typedef NTSTATUS(NTAPI *WOW64WIN_INITIALIZE)(void);

#endif
