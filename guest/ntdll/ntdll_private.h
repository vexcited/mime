/*
 * ntdll internal interface.
 */
#ifndef MIME_NTDLL_PRIVATE_H
#define MIME_NTDLL_PRIVATE_H

/* the system service stubs and the runtime live in ntdll itself. */
#define MIME_NTDLL
#define NTSYSCALLAPI
#include <mime/ntrtl.h>
#include <stdarg.h>

#define NTDLL_EXPORT

typedef void(NTAPI *PRTL_THREAD_START)(PVOID entry, PVOID parameter);

extern PVOID LdrpNtdllBase;
extern RTL_CRITICAL_SECTION LdrpLoaderLock;
extern PVOID LdrpKernel32ThreadInitThunk; /* BaseThreadInitThunk, set when kernel32 loads */

#if defined(__x86_64__) || defined(__i386__)
#define YieldProcessor() __asm__ volatile("pause")
#else
#define YieldProcessor() __asm__ volatile("yield")
#endif

FORCEINLINE HANDLE NtCurrentThreadId(void)
{
    return NtCurrentTeb()->ClientId.UniqueThread;
}

void LdrpInitializeProcessHeap(void);
PVOID LdrpImageDirectory(PVOID base, USHORT index, PULONG size);
PIMAGE_NT_HEADERS LdrpNtHeaders(PVOID base);
void NTAPI KiUserExceptionDispatcherC(PEXCEPTION_RECORD record, PCONTEXT context);

#endif
